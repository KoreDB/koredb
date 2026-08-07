#include <unordered_set>

#include "binder/expression/expression_util.h"
#include "main/client_context.h"
#include "planner/operator/logical_hash_join.h"
#include "processor/operator/hash_join/hash_join_build.h"
#include "processor/operator/hash_join/hash_join_probe.h"
#include "processor/plan_mapper.h"

using namespace kuzu::binder;
using namespace kuzu::planner;
using namespace kuzu::common;

namespace kuzu {
namespace processor {

HashJoinBuildInfo PlanMapper::createHashBuildInfo(const Schema& buildSideSchema,
    const expression_vector& keys, const expression_vector& payloads) {
    f_group_pos_set keyGroupPosSet;
    std::vector<DataPos> keysPos;
    std::vector<FStateType> fStateTypes;
    std::vector<DataPos> payloadsPos;
    auto tableSchema = FactorizedTableSchema();
    for (auto& key : keys) {
        auto pos = DataPos(buildSideSchema.getExpressionPos(*key));
        keyGroupPosSet.insert(pos.dataChunkPos);
        // Keys are always stored in flat column.
        auto columnSchema = ColumnSchema(false /* isUnFlat */, pos.dataChunkPos,
            LogicalTypeUtils::getRowLayoutSize(key->dataType));
        tableSchema.appendColumn(std::move(columnSchema));
        keysPos.push_back(pos);
        fStateTypes.push_back(buildSideSchema.getGroup(pos.dataChunkPos)->isFlat() ?
                                  FStateType::FLAT :
                                  FStateType::UNFLAT);
    }
    for (auto& payload : payloads) {
        auto pos = DataPos(buildSideSchema.getExpressionPos(*payload));
        if (keyGroupPosSet.contains(pos.dataChunkPos) ||
            buildSideSchema.getGroup(pos.dataChunkPos)->isFlat()) {
            // Payloads need to be stored in flat column in 2 cases
            // 1. payload is in the same chunk as a key. Since keys are always stored as flat,
            // payloads must also be stored as flat.
            // 2. payload is in flat chunk
            auto columnSchema = ColumnSchema(false /* isUnFlat */, pos.dataChunkPos,
                LogicalTypeUtils::getRowLayoutSize(payload->dataType));
            tableSchema.appendColumn(std::move(columnSchema));
        } else {
            auto columnSchema =
                ColumnSchema(true /* isUnFlat */, pos.dataChunkPos, sizeof(overflow_value_t));
            tableSchema.appendColumn(std::move(columnSchema));
        }
        payloadsPos.push_back(pos);
    }
    auto hashValueColumn = ColumnSchema(false /* isUnFlat */, INVALID_DATA_CHUNK_POS,
        LogicalTypeUtils::getRowLayoutSize(LogicalType::HASH()));
    tableSchema.appendColumn(std::move(hashValueColumn));
    auto pointerColumn = ColumnSchema(false /* isUnFlat */, INVALID_DATA_CHUNK_POS,
        LogicalTypeUtils::getRowLayoutSize(LogicalType::INT64()));
    tableSchema.appendColumn(std::move(pointerColumn));
    return HashJoinBuildInfo(std::move(keysPos), std::move(fStateTypes), std::move(payloadsPos),
        std::move(tableSchema));
}

// True iff `all` (columns located by dataChunkPos in `schema`) is an *appendable* factorization for
// the Grace executor's scatter: at most ONE unflat data chunk among them, and every key in a SINGLE
// data chunk. The executor routes a side by hash(key): an unflat key scatters element-wise while flat
// columns broadcast and unflat columns sharing the key's chunk travel with it; a flat key routes the
// whole group to one partition. More than one unflat chunk would be a cross-product the flatten-on-
// append cannot represent, and keys spread across chunks cannot be scattered by one selection state.
static bool isAppendableFactorization(const Schema& schema, const std::vector<DataPos>& all,
    const std::unordered_set<common::idx_t>& keyGroups) {
    if (keyGroups.size() != 1) {
        return false;
    }
    common::idx_t unflatGroup = INVALID_DATA_CHUNK_POS;
    for (auto& p : all) {
        if (schema.getGroup(p.dataChunkPos)->isFlat()) {
            continue;
        }
        if (unflatGroup != INVALID_DATA_CHUNK_POS && p.dataChunkPos != unflatGroup) {
            return false; // more than one unflat chunk
        }
        unflatGroup = p.dataChunkPos;
    }
    return true;
}

// Computes the static (plan-time) metadata that lets the HASH_JOIN operator run the out-of-core
// (Grace) path when `spill_hash_join` is enabled. Eligible shapes: (a) INNER/LEFT with non-empty
// build payloads, or (b) MARK (EXISTS/semi) with keys-only build and a single-chunk output (all probe
// columns + the mark in one data chunk); in both cases no nested/NODE/REL column, and each side
// (build; probe) an *appendable* factorization (at most one unflat chunk, keys in a single chunk --
// see isAppendableFactorization). This covers the flat single-chunk join, the factorized RETURN
// *-style join the planner actually produces (e.g. an unflat-key build with a flat build root), and
// the single-chunk EXISTS/anti-join. `multiChunkOutput` records whether the output spans several
// chunks, which selects the probe operator's emission path (materialized+scan vs flat streaming; MARK
// is single-chunk only). Every other shape keeps `eligible == false` (in-memory path).
static GraceHashJoinInfo computeGraceHashJoinInfo(const LogicalHashJoin& hashJoin,
    const Schema& outSchema, const Schema& buildSchema, const expression_vector& probeKeys,
    const expression_vector& payloads, const std::vector<LogicalType>& buildKeyTypes,
    const std::vector<DataPos>& buildAllPos, const std::vector<DataPos>& probeKeysDataPos,
    const std::vector<DataPos>& probePayloadsOutPos) {
    GraceHashJoinInfo info;
    info.joinType = hashJoin.getJoinType();
    const auto jt = hashJoin.getJoinType();
    const bool isMark = (jt == JoinType::MARK) && hashJoin.hasMark();
    const bool isCount = (jt == JoinType::COUNT);
    // Supported shapes: INNER/LEFT with build payloads (computeJoin null-pads a LEFT probe row that
    // finds no build match, NULL-key rows included); MARK (EXISTS/semi) whose build side materializes
    // only keys (empty payloads) and adds a single BOOL mark column per probe row; or COUNT
    // (size/COUNT{} subquery) whose build is pre-aggregated to (key, count) -- one INT64 count payload
    // read per probe row (0 when absent). Every other shape keeps the in-memory path.
    if (isMark) {
        if (!payloads.empty()) {
            return info; // a MARK join carrying build payloads is not the shape we handle
        }
    } else if (isCount) {
        if (payloads.size() != 1 || hashJoin.hasMark()) {
            return info; // COUNT join is a single pre-aggregated count payload, no mark
        }
    } else if (!((jt == JoinType::INNER || jt == JoinType::LEFT) && !hashJoin.hasMark() &&
                   !payloads.empty())) {
        return info;
    }
    auto sameChunk = [](const std::vector<DataPos>& ps) {
        if (ps.empty()) {
            return true;
        }
        const auto c = ps[0].dataChunkPos;
        for (auto& p : ps) {
            if (p.dataChunkPos != c) {
                return false;
            }
        }
        return true;
    };
    // Probe non-key output columns = everything in scope that is neither a build payload nor a probe
    // key (the mark is excluded by the no-mark requirement above), captured/re-emitted alongside the
    // probe keys.
    std::unordered_set<std::string> excluded;
    for (auto& p : payloads) {
        excluded.insert(p->getUniqueName());
    }
    for (auto& k : probeKeys) {
        excluded.insert(k->getUniqueName());
    }
    // The mark is produced by the join, not carried by the probe; exclude it from the probe non-key
    // columns (it is written into its own output vector).
    if (isMark) {
        excluded.insert(hashJoin.getMark()->getUniqueName());
    }
    std::vector<DataPos> outputAllPos = probeKeysDataPos;
    outputAllPos.insert(outputAllPos.end(), probePayloadsOutPos.begin(), probePayloadsOutPos.end());
    expression_vector probeNonKeyExprs;
    std::vector<DataPos> probeNonKeyPos;
    for (auto& expr : outSchema.getExpressionsInScope()) {
        if (excluded.contains(expr->getUniqueName())) {
            continue;
        }
        const auto dp = DataPos(outSchema.getExpressionPos(*expr));
        outputAllPos.push_back(dp);
        probeNonKeyExprs.push_back(expr);
        probeNonKeyPos.push_back(dp);
    }
    // The mark column is part of the output, so it must join the single-chunk determination below (it
    // is written alongside the probe columns), but it is not a probe input column stored in a partition.
    if (isMark) {
        outputAllPos.push_back(DataPos(outSchema.getExpressionPos(*hashJoin.getMark())));
    }
    if (outputAllPos.empty()) {
        return info;
    }
    // Exclude nested/NODE/REL columns (build payloads, probe keys, probe non-keys). Such a value
    // occupies a single schema position but expands to multiple runtime vectors, breaking the
    // executor's one-column-per-expression model; those fall back to the proven in-memory path.
    auto anyNested = [](const expression_vector& exprs) {
        for (auto& e : exprs) {
            if (LogicalTypeUtils::isNested(e->getDataType())) {
                return true;
            }
        }
        return false;
    };
    if (anyNested(payloads) || anyNested(probeKeys) || anyNested(probeNonKeyExprs)) {
        return info;
    }
    // Each side must be an appendable factorization (see isAppendableFactorization). Build columns
    // (keys + payloads) are located in the build schema; probe columns (keys + non-keys) are located
    // in the output schema (the probe writes them into the join's output result set).
    const auto numKeys = probeKeys.size();
    std::unordered_set<common::idx_t> buildKeyGroups;
    for (auto i = 0u; i < numKeys; i++) {
        buildKeyGroups.insert(buildAllPos[i].dataChunkPos);
    }
    if (!isAppendableFactorization(buildSchema, buildAllPos, buildKeyGroups)) {
        return info;
    }
    std::unordered_set<common::idx_t> probeKeyGroups;
    for (auto& p : probeKeysDataPos) {
        probeKeyGroups.insert(p.dataChunkPos);
    }
    std::vector<DataPos> probeAllPos = probeKeysDataPos;
    probeAllPos.insert(probeAllPos.end(), probeNonKeyPos.begin(), probeNonKeyPos.end());
    if (!isAppendableFactorization(outSchema, probeAllPos, probeKeyGroups)) {
        return info;
    }
    // Single-chunk output -> materialize + scan back into that one chunk; multi-chunk output -> stream
    // the join one flat tuple at a time (correct for any chunk structure).
    info.multiChunkOutput = !sameChunk(outputAllPos);
    if ((isMark || isCount) && info.multiChunkOutput) {
        // Only the single-chunk MARK/COUNT shape (all probe columns + the mark/count in one data chunk)
        // is handled; a multi-chunk output falls back to the in-memory path.
        return info; // eligible stays false
    }
    info.eligible = true;
    for (auto& t : buildKeyTypes) {
        info.keyTypes.push_back(t.copy());
    }
    for (auto& p : payloads) {
        info.buildPayloadTypes.push_back(p->getDataType().copy());
    }
    for (auto i = 0u; i < probeNonKeyExprs.size(); i++) {
        info.probeNonKeyTypes.push_back(probeNonKeyExprs[i]->getDataType().copy());
        info.probeNonKeyPos.push_back(probeNonKeyPos[i]);
    }
    return info;
}

std::unique_ptr<PhysicalOperator> PlanMapper::mapHashJoin(const LogicalOperator* logicalOperator) {
    auto hashJoin = logicalOperator->constPtrCast<LogicalHashJoin>();
    auto outSchema = hashJoin->getSchema();
    auto buildSchema = hashJoin->getChild(1)->getSchema();
    std::unique_ptr<PhysicalOperator> probeSidePrevOperator;
    std::unique_ptr<PhysicalOperator> buildSidePrevOperator;
    // Map the side into which semi mask is passed first.
    if (hashJoin->getSIPInfo().dependency == SIPDependency::PROBE_DEPENDS_ON_BUILD) {
        buildSidePrevOperator = mapOperator(hashJoin->getChild(1).get());
        probeSidePrevOperator = mapOperator(hashJoin->getChild(0).get());
    } else {
        probeSidePrevOperator = mapOperator(hashJoin->getChild(0).get());
        buildSidePrevOperator = mapOperator(hashJoin->getChild(1).get());
    }
    expression_vector probeKeys;
    expression_vector buildKeys;
    for (auto& [probeKey, buildKey] : hashJoin->getJoinConditions()) {
        probeKeys.push_back(probeKey);
        buildKeys.push_back(buildKey);
    }
    auto buildKeyTypes = ExpressionUtil::getDataTypes(buildKeys);
    auto payloads =
        ExpressionUtil::excludeExpressions(hashJoin->getExpressionsToMaterialize(), probeKeys);
    // Create build
    auto buildInfo = createHashBuildInfo(*buildSchema, buildKeys, payloads);
    // Capture the build-side column data positions before buildInfo is moved into the operator, so
    // the Grace eligibility check below can verify the build side is a single data chunk.
    std::vector<DataPos> graceBuildAllPos = buildInfo.keysPos;
    graceBuildAllPos.insert(graceBuildAllPos.end(), buildInfo.payloadsPos.begin(),
        buildInfo.payloadsPos.end());
    auto globalHashTable = std::make_unique<JoinHashTable>(*clientContext->getMemoryManager(),
        LogicalType::copy(buildKeyTypes), buildInfo.tableSchema.copy());
    auto sharedState = std::make_shared<HashJoinSharedState>(std::move(globalHashTable));
    auto buildPrintInfo = std::make_unique<HashJoinBuildPrintInfo>(buildKeys, payloads);
    auto hashJoinBuild = std::make_unique<HashJoinBuild>(PhysicalOperatorType::HASH_JOIN_BUILD,
        sharedState, std::move(buildInfo), std::move(buildSidePrevOperator), getOperatorID(),
        buildPrintInfo->copy());
    hashJoinBuild->setDescriptor(std::make_unique<ResultSetDescriptor>(buildSchema));
    // Create probe
    std::vector<DataPos> probeKeysDataPos;
    for (auto& probeKey : probeKeys) {
        probeKeysDataPos.emplace_back(outSchema->getExpressionPos(*probeKey));
    }
    std::vector<DataPos> probePayloadsOutPos;
    for (auto& payload : payloads) {
        probePayloadsOutPos.emplace_back(outSchema->getExpressionPos(*payload));
    }
    ProbeDataInfo probeDataInfo(probeKeysDataPos, probePayloadsOutPos);
    if (hashJoin->hasMark()) {
        auto mark = hashJoin->getMark();
        auto markOutputPos = DataPos(outSchema->getExpressionPos(*mark));
        probeDataInfo.markDataPos = markOutputPos;
    } else {
        probeDataInfo.markDataPos = DataPos::getInvalidPos();
    }
    sharedState->setGraceInfo(computeGraceHashJoinInfo(*hashJoin, *outSchema, *buildSchema, probeKeys,
        payloads, buildKeyTypes, graceBuildAllPos, probeKeysDataPos, probePayloadsOutPos));
    auto probePrintInfo = std::make_unique<HashJoinProbePrintInfo>(probeKeys);
    auto hashJoinProbe = make_unique<HashJoinProbe>(sharedState, hashJoin->getJoinType(),
        hashJoin->requireFlatProbeKeys(), probeDataInfo, std::move(probeSidePrevOperator),
        getOperatorID(), probePrintInfo->copy());
    hashJoinProbe->addChild(std::move(hashJoinBuild));
    if (hashJoin->getSIPInfo().direction == SIPDirection::PROBE_TO_BUILD) {
        mapSIPJoin(hashJoinProbe.get());
    }
    return hashJoinProbe;
}

} // namespace processor
} // namespace kuzu
