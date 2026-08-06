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

// Computes the static (plan-time) metadata that lets the HASH_JOIN operator run the out-of-core
// (Grace) path when `spill_hash_join` is enabled. Conservative: only INNER joins with no mark,
// non-empty build payloads, a single-chunk build side, and all output columns in one data chunk are
// marked eligible; every other shape keeps `eligible == false` and uses the in-memory path.
static GraceHashJoinInfo computeGraceHashJoinInfo(const LogicalHashJoin& hashJoin,
    const Schema& outSchema, const expression_vector& probeKeys, const expression_vector& payloads,
    const std::vector<LogicalType>& buildKeyTypes, const std::vector<DataPos>& buildAllPos,
    const std::vector<DataPos>& probeKeysDataPos, const std::vector<DataPos>& probePayloadsOutPos) {
    GraceHashJoinInfo info;
    info.joinType = hashJoin.getJoinType();
    const auto jt = hashJoin.getJoinType();
    // v1 supports INNER only. The executor also implements LEFT, but the operator's null-padding
    // path is not yet end-to-end verified, so LEFT joins keep the in-memory path for now.
    bool eligible = jt == JoinType::INNER && !hashJoin.hasMark() && !payloads.empty();
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
    // Build side must be one data chunk (all columns stored flat, one append state).
    if (eligible && !sameChunk(buildAllPos)) {
        eligible = false;
    }
    // Probe output columns = everything in scope that is neither a build payload nor a probe key
    // (the mark is excluded by the no-mark requirement above). These are the probe non-key columns
    // that must be captured and re-emitted; the probe keys are captured separately.
    std::unordered_set<std::string> excluded;
    for (auto& p : payloads) {
        excluded.insert(p->getUniqueName());
    }
    for (auto& k : probeKeys) {
        excluded.insert(k->getUniqueName());
    }
    std::vector<DataPos> outputAllPos = probeKeysDataPos;
    outputAllPos.insert(outputAllPos.end(), probePayloadsOutPos.begin(), probePayloadsOutPos.end());
    expression_vector probeNonKeyExprs;
    if (eligible) {
        for (auto& expr : outSchema.getExpressionsInScope()) {
            if (excluded.contains(expr->getUniqueName())) {
                continue;
            }
            outputAllPos.push_back(DataPos(outSchema.getExpressionPos(*expr)));
            probeNonKeyExprs.push_back(expr);
        }
    }
    // The operator materializes the join and scans it back into one output chunk, so every output
    // column must live in a single data chunk. The chunk may be flat (one row emitted per call) or
    // unflat (a vector of rows per call); the operator adapts at runtime.
    if (eligible && (outputAllPos.empty() || !sameChunk(outputAllPos))) {
        eligible = false;
    }
    if (!eligible) {
        return info; // eligible stays false -> operator uses the in-memory path
    }
    info.eligible = true;
    for (auto& t : buildKeyTypes) {
        info.keyTypes.push_back(t.copy());
    }
    for (auto& p : payloads) {
        info.buildPayloadTypes.push_back(p->getDataType().copy());
    }
    for (auto& e : probeNonKeyExprs) {
        info.probeNonKeyTypes.push_back(e->getDataType().copy());
        info.probeNonKeyPos.push_back(DataPos(outSchema.getExpressionPos(*e)));
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
    sharedState->setGraceInfo(computeGraceHashJoinInfo(*hashJoin, *outSchema, probeKeys, payloads,
        buildKeyTypes, graceBuildAllPos, probeKeysDataPos, probePayloadsOutPos));
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
