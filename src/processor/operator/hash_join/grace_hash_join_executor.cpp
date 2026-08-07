#include "processor/operator/hash_join/grace_hash_join_executor.h"

#include <numeric>

#include "common/data_chunk/data_chunk_state.h"
#include "common/vector/value_vector.h"
#include "function/hash/vector_hash_functions.h"
#include "processor/data_pos.h"
#include "processor/operator/hash_join/join_hash_table.h"
#include "processor/result/factorized_table_util.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;
using namespace kuzu::function;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

static std::vector<LogicalType> copyTypes(const std::vector<LogicalType>& types) {
    std::vector<LogicalType> result;
    result.reserve(types.size());
    for (auto& t : types) {
        result.push_back(t.copy());
    }
    return result;
}

static std::vector<LogicalType> concatTypes(const std::vector<LogicalType>& a,
    const std::vector<LogicalType>& b) {
    auto result = copyTypes(a);
    for (auto& t : b) {
        result.push_back(t.copy());
    }
    return result;
}

GraceHashJoinExecutor::GraceHashJoinExecutor(MemoryManager* mm, VirtualFileSystem* vfs,
    std::string buildSpillPath, std::string probeSpillPath, std::vector<LogicalType> keyTypes,
    std::vector<LogicalType> buildPayloadTypes, std::vector<LogicalType> probePayloadTypes,
    idx_t logNumPartitions, uint64_t memoryBudgetBytes)
    : mm{mm}, keyTypes{std::move(keyTypes)}, buildPayloadTypes{std::move(buildPayloadTypes)},
      probePayloadTypes{std::move(probePayloadTypes)},
      numKeys{static_cast<common::idx_t>(this->keyTypes.size())},
      memoryBudgetBytes{memoryBudgetBytes},
      buildParts{mm, concatTypes(this->keyTypes, this->buildPayloadTypes), logNumPartitions, vfs,
          std::move(buildSpillPath)},
      probeParts{mm, concatTypes(this->keyTypes, this->probePayloadTypes), logNumPartitions, vfs,
          std::move(probeSpillPath)},
      hashVector{std::make_unique<ValueVector>(LogicalType::HASH(), mm)},
      tmpHashVector{std::make_unique<ValueVector>(LogicalType::HASH(), mm)} {}

FactorizedTableSchema GraceHashJoinExecutor::makeJoinHashTableSchema() const {
    FactorizedTableSchema schema;
    for (auto& t : keyTypes) {
        schema.appendColumn(
            ColumnSchema(false /*isUnFlat*/, 0, LogicalTypeUtils::getRowLayoutSize(t)));
    }
    for (auto& t : buildPayloadTypes) {
        schema.appendColumn(ColumnSchema(false, 0, LogicalTypeUtils::getRowLayoutSize(t)));
    }
    schema.appendColumn(ColumnSchema(false, INVALID_DATA_CHUNK_POS,
        LogicalTypeUtils::getRowLayoutSize(LogicalType::HASH())));
    schema.appendColumn(ColumnSchema(false, INVALID_DATA_CHUNK_POS,
        LogicalTypeUtils::getRowLayoutSize(LogicalType::INT64())));
    return schema;
}

FactorizedTableSchema GraceHashJoinExecutor::makeOutputSchema() const {
    auto types = concatTypes(keyTypes, probePayloadTypes);
    for (auto& t : buildPayloadTypes) {
        types.push_back(t.copy());
    }
    return FactorizedTableUtils::createFlatTableSchema(std::move(types));
}

void GraceHashJoinExecutor::appendToPartitions(PartitionedFactorizedTable& parts,
    const std::vector<ValueVector*>& keyVectors, const std::vector<ValueVector*>& payloadVectors) {
    auto* state = keyVectors[0]->state.get();
    auto& sel = state->getSelVector();
    // Routing hash: hash the first key, then combine the rest. Same hash the JoinHashTable computes
    // internally, so build and probe are co-partitioned.
    VectorHashFunction::computeHash(*keyVectors[0], sel, *hashVector, sel);
    for (auto i = 1u; i < keyVectors.size(); i++) {
        VectorHashFunction::computeHash(*keyVectors[i], sel, *tmpHashVector, sel);
        VectorHashFunction::combineHash(*hashVector, sel, *tmpHashVector, sel, *hashVector, sel);
    }
    std::vector<ValueVector*> allVectors;
    allVectors.reserve(keyVectors.size() + payloadVectors.size());
    allVectors.insert(allVectors.end(), keyVectors.begin(), keyVectors.end());
    allVectors.insert(allVectors.end(), payloadVectors.begin(), payloadVectors.end());
    if (state->isFlat()) {
        // A flat (single-row) key input -- e.g. a probe side rooted at the join key, carrying an unflat
        // payload group -- has no batch to scatter by key; route the whole group (flat key broadcast +
        // any unflat payload flattened) to the single partition hash(key) selects.
        parts.appendFactorizedGroup(allVectors, hashVector->getValue<hash_t>(sel[0]));
    } else {
        // Unflat key: scatter each key element to its partition (flat payloads broadcast, an unflat
        // payload sharing the key's state travels element-wise).
        parts.appendVectors(allVectors, *hashVector);
    }
    parts.spillToReduceResidentBytesTo(memoryBudgetBytes);
}

void GraceHashJoinExecutor::appendBuild(const std::vector<ValueVector*>& keyVectors,
    const std::vector<ValueVector*>& payloadVectors) {
    appendToPartitions(buildParts, keyVectors, payloadVectors);
}

void GraceHashJoinExecutor::appendProbe(const std::vector<ValueVector*>& keyVectors,
    const std::vector<ValueVector*>& payloadVectors) {
    appendToPartitions(probeParts, keyVectors, payloadVectors);
}

void GraceHashJoinExecutor::appendBuildFactorized(const std::vector<ValueVector*>& keyVectors,
    const std::vector<ValueVector*>& payloadVectors) {
    auto* keyState = keyVectors[0]->state.get();
    auto& keySel = keyState->getSelVector();
    // The factorized build shape is one flat key (broadcast) + one unflat payload group per call.
    KU_ASSERT(keyState->isFlat() && !payloadVectors.empty());
    // Routing hash over the flat key(s): same hash the JoinHashTable computes internally, so the
    // build and probe sides stay co-partitioned.
    VectorHashFunction::computeHash(*keyVectors[0], keySel, *hashVector, keySel);
    for (auto i = 1u; i < keyVectors.size(); i++) {
        VectorHashFunction::computeHash(*keyVectors[i], keySel, *tmpHashVector, keySel);
        VectorHashFunction::combineHash(*hashVector, keySel, *tmpHashVector, keySel, *hashVector,
            keySel);
    }
    const auto keyHash = hashVector->getValue<hash_t>(keySel[0]);
    std::vector<ValueVector*> allVectors;
    allVectors.reserve(keyVectors.size() + payloadVectors.size());
    allVectors.insert(allVectors.end(), keyVectors.begin(), keyVectors.end());
    allVectors.insert(allVectors.end(), payloadVectors.begin(), payloadVectors.end());
    buildParts.appendFactorizedGroup(allVectors, keyHash);
    buildParts.spillToReduceResidentBytesTo(memoryBudgetBytes);
}

std::unique_ptr<JoinHashTable> GraceHashJoinExecutor::buildHashTableForPartition(idx_t p) {
    auto jht = std::make_unique<JoinHashTable>(*mm, copyTypes(keyTypes), makeJoinHashTableSchema());
    auto& buildPart = buildParts.getResidentPartition(p);
    if (buildPart.getNumTuples() > 0) {
        auto scanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        std::vector<std::unique_ptr<ValueVector>> holders;
        std::vector<ValueVector*> bKeyVecs, bPayVecs, bAllVecs;
        for (auto& t : keyTypes) {
            auto v = std::make_unique<ValueVector>(t.copy(), mm);
            v->state = scanState;
            bKeyVecs.push_back(v.get());
            bAllVecs.push_back(v.get());
            holders.push_back(std::move(v));
        }
        for (auto& t : buildPayloadTypes) {
            auto v = std::make_unique<ValueVector>(t.copy(), mm);
            v->state = scanState;
            bPayVecs.push_back(v.get());
            bAllVecs.push_back(v.get());
            holders.push_back(std::move(v));
        }
        for (uint64_t t = 0; t < buildPart.getNumTuples(); t += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, buildPart.getNumTuples() - t);
            scanState->initOriginalAndSelectedSize(m);
            buildPart.scan(std::span<ValueVector*>(bAllVecs), t, m);
            jht->appendVectors(bKeyVecs, bPayVecs, scanState.get());
        }
        jht->allocateHashSlots(jht->getNumEntries());
        jht->buildHashSlots();
    }
    return jht;
}

std::unique_ptr<FactorizedTable> GraceHashJoinExecutor::computeJoin(bool isLeftJoin) {
    auto output = std::make_unique<FactorizedTable>(mm, makeOutputSchema());
    const auto numBuildPayloads = buildPayloadTypes.size();

    // Flat probe-row vectors: hold one probe row and serve as its output key/payload columns.
    auto flatState = DataChunkState::getSingleValueDataChunkState();
    std::vector<std::unique_ptr<ValueVector>> probeHolders;
    std::vector<ValueVector*> probeKeyVecs, probePayloadVecs;
    for (auto& t : keyTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = flatState;
        probeKeyVecs.push_back(v.get());
        probeHolders.push_back(std::move(v));
    }
    for (auto& t : probePayloadTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = flatState;
        probePayloadVecs.push_back(v.get());
        probeHolders.push_back(std::move(v));
    }
    // Unflat build-payload output vectors, populated per probe row from matched tuples.
    auto buildOutState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    std::vector<std::unique_ptr<ValueVector>> buildOutHolders;
    std::vector<ValueVector*> buildPayloadVecs;
    for (auto& t : buildPayloadTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = buildOutState;
        buildPayloadVecs.push_back(v.get());
        buildOutHolders.push_back(std::move(v));
    }
    std::vector<uint32_t> buildPayloadColIdxs(numBuildPayloads);
    std::iota(buildPayloadColIdxs.begin(), buildPayloadColIdxs.end(),
        static_cast<uint32_t>(numKeys));

    // Output vector list: [probeKeys..., probePayloads..., buildPayloads...].
    std::vector<ValueVector*> outputVecs;
    outputVecs.insert(outputVecs.end(), probeKeyVecs.begin(), probeKeyVecs.end());
    outputVecs.insert(outputVecs.end(), probePayloadVecs.begin(), probePayloadVecs.end());
    outputVecs.insert(outputVecs.end(), buildPayloadVecs.begin(), buildPayloadVecs.end());

    // Probe scratch.
    SelectionVector hashSelVec(DEFAULT_VECTOR_CAPACITY);
    ValueVector probeHashVec(LogicalType::HASH(), mm);
    ValueVector probeTmpHashVec(LogicalType::HASH(), mm);
    auto probedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);
    auto matchedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);

    for (idx_t p = 0; p < buildParts.getNumPartitions(); p++) {
        // Build a JoinHashTable from the (reloaded) build partition p.
        auto jht = buildHashTableForPartition(p);

        auto& probePart = probeParts.getResidentPartition(p);
        // For an inner join an empty build partition yields no matches; for a left join its probe
        // rows still emit null-padded output, so we always process the probe partition.
        if (probePart.getNumTuples() > 0) {
            auto pScanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
            std::vector<std::unique_ptr<ValueVector>> holders;
            std::vector<ValueVector*> pKeyVecs, pPayVecs, pAllVecs;
            for (auto& t : keyTypes) {
                auto v = std::make_unique<ValueVector>(t.copy(), mm);
                v->state = pScanState;
                pKeyVecs.push_back(v.get());
                pAllVecs.push_back(v.get());
                holders.push_back(std::move(v));
            }
            for (auto& t : probePayloadTypes) {
                auto v = std::make_unique<ValueVector>(t.copy(), mm);
                v->state = pScanState;
                pPayVecs.push_back(v.get());
                pAllVecs.push_back(v.get());
                holders.push_back(std::move(v));
            }
            for (uint64_t t = 0; t < probePart.getNumTuples(); t += DEFAULT_VECTOR_CAPACITY) {
                const auto m =
                    std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, probePart.getNumTuples() - t);
                pScanState->initOriginalAndSelectedSize(m);
                probePart.scan(std::span<ValueVector*>(pAllVecs), t, m);
                for (uint64_t r = 0; r < m; r++) {
                    // A NULL join key never matches in an equi-join; the in-memory path discards such
                    // rows before probing, so skip probing here too (a LEFT join still null-pads).
                    bool keyIsNull = false;
                    for (auto i = 0u; i < numKeys; i++) {
                        if (pKeyVecs[i]->isNull(r)) {
                            keyIsNull = true;
                            break;
                        }
                    }
                    for (auto i = 0u; i < numKeys; i++) {
                        probeKeyVecs[i]->copyFromVectorData(0, pKeyVecs[i], r);
                    }
                    for (auto i = 0u; i < probePayloadVecs.size(); i++) {
                        probePayloadVecs[i]->copyFromVectorData(0, pPayVecs[i], r);
                    }
                    flatState->getSelVectorUnsafe().setToUnfiltered(1);
                    probedTuples[0] = nullptr;
                    uint64_t rowMatches = 0;
                    if (!keyIsNull && jht->getNumEntries() > 0) {
                        jht->probe(probeKeyVecs, probeHashVec, hashSelVec,
                            numKeys > 1 ? &probeTmpHashVec : nullptr, probedTuples.get());
                    }
                    while (probedTuples[0] != nullptr) {
                        const auto numMatched = jht->matchFlatKeys(probeKeyVecs, probedTuples.get(),
                            matchedTuples.get());
                        if (numMatched > 0) {
                            buildOutState->initOriginalAndSelectedSize(numMatched);
                            jht->lookup(buildPayloadVecs, buildPayloadColIdxs, matchedTuples.get(),
                                0, numMatched);
                            output->append(outputVecs);
                            rowMatches += numMatched;
                        }
                        if (numMatched < DEFAULT_VECTOR_CAPACITY) {
                            break;
                        }
                    }
                    if (isLeftJoin && rowMatches == 0) {
                        // Null-pad: one output row, probe columns kept, build payloads null.
                        buildOutState->initOriginalAndSelectedSize(1);
                        for (auto* buildVec : buildPayloadVecs) {
                            buildVec->setNull(0, true);
                        }
                        output->append(outputVecs);
                    }
                }
            }
        }
        buildParts.freePartition(p);
        probeParts.freePartition(p);
    }
    return output;
}

void GraceHashJoinExecutor::initProbeStream(std::vector<ValueVector*> probeOutVecs,
    std::vector<ValueVector*> buildOutVecs, bool isLeftJoin) {
    KU_ASSERT(probeOutVecs.size() == numKeys + probePayloadTypes.size());
    KU_ASSERT(buildOutVecs.size() == buildPayloadTypes.size());
    streamProbeOutVecs = std::move(probeOutVecs);
    streamBuildOutVecs = std::move(buildOutVecs);
    streamProbeKeyVecs.assign(streamProbeOutVecs.begin(),
        streamProbeOutVecs.begin() + static_cast<int64_t>(numKeys));
    streamBuildState = streamBuildOutVecs.empty() ? nullptr : streamBuildOutVecs[0]->state.get();
    streamLeftJoin = isLeftJoin;
    streamBuildColIdxs.resize(buildPayloadTypes.size());
    std::iota(streamBuildColIdxs.begin(), streamBuildColIdxs.end(),
        static_cast<uint32_t>(numKeys));

    streamHashVec = std::make_unique<ValueVector>(LogicalType::HASH(), mm);
    if (numKeys > 1) {
        streamTmpHashVec = std::make_unique<ValueVector>(LogicalType::HASH(), mm);
    }
    streamHashSelVec = std::make_unique<SelectionVector>(DEFAULT_VECTOR_CAPACITY);
    streamProbedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);
    streamMatchedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);

    // Unflat buffer for scanning the probe partition a vector at a time; per-row values are copied
    // out into the flat probe output vectors.
    streamProbeScanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    streamProbeScanHolders.clear();
    streamProbeScanVecs.clear();
    for (auto& t : keyTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = streamProbeScanState;
        streamProbeScanVecs.push_back(v.get());
        streamProbeScanHolders.push_back(std::move(v));
    }
    for (auto& t : probePayloadTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = streamProbeScanState;
        streamProbeScanVecs.push_back(v.get());
        streamProbeScanHolders.push_back(std::move(v));
    }

    streamNextPartition = 0;
    streamHasPartition = false;
    streamProbePart = nullptr;
    streamProbeNumRows = 0;
    streamProbeRowIdx = 0;
    streamBlockSize = 0;
    streamBlockOffset = 0;
    streamRowActive = false;
    streamRowMatchCount = 0;
    streamInitialized = true;
}

bool GraceHashJoinExecutor::streamLoadNextPartition() {
    if (streamHasPartition) {
        buildParts.freePartition(streamCurPartition);
        probeParts.freePartition(streamCurPartition);
        streamHasPartition = false;
        streamJHT.reset();
        streamProbePart = nullptr;
    }
    while (streamNextPartition < buildParts.getNumPartitions()) {
        const auto p = streamNextPartition++;
        auto& probePart = probeParts.getResidentPartition(p);
        if (probePart.getNumTuples() == 0) {
            // No probe rows here -> no output regardless of join type; free both sides and move on.
            buildParts.freePartition(p);
            probeParts.freePartition(p);
            continue;
        }
        streamCurPartition = p;
        streamJHT = buildHashTableForPartition(p);
        streamProbePart = &probePart;
        streamProbeNumRows = probePart.getNumTuples();
        streamProbeRowIdx = 0;
        streamBlockSize = 0;
        streamBlockOffset = 0;
        streamHasPartition = true;
        return true;
    }
    return false;
}

void GraceHashJoinExecutor::streamLoadProbeRow() {
    if (streamBlockOffset >= streamBlockSize) {
        const auto remaining = streamProbeNumRows - streamProbeRowIdx;
        const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, remaining);
        streamProbeScanState->initOriginalAndSelectedSize(m);
        streamProbePart->scan(std::span<ValueVector*>(streamProbeScanVecs), streamProbeRowIdx, m);
        streamBlockSize = m;
        streamBlockOffset = 0;
    }
    // Copy this row into the flat (single-value) probe output vectors.
    for (auto i = 0u; i < streamProbeOutVecs.size(); i++) {
        streamProbeOutVecs[i]->state->getSelVectorUnsafe().setToUnfiltered(1);
        streamProbeOutVecs[i]->copyFromVectorData(0, streamProbeScanVecs[i], streamBlockOffset);
    }
    streamBlockOffset++;
    streamProbeRowIdx++;
}

bool GraceHashJoinExecutor::getNextChunk() {
    KU_ASSERT(streamInitialized);
    while (true) {
        // (1) Ship the next batch of matches for the active probe row, if any.
        if (streamRowActive) {
            auto* probed = streamProbedTuples.get();
            bool shipped = false;
            if (probed[0] != nullptr) {
                const auto numMatched =
                    streamJHT->matchFlatKeys(streamProbeKeyVecs, probed, streamMatchedTuples.get());
                if (numMatched > 0) {
                    if (streamBuildState != nullptr) {
                        streamBuildState->initOriginalAndSelectedSize(numMatched);
                    }
                    streamJHT->lookup(streamBuildOutVecs, streamBuildColIdxs,
                        streamMatchedTuples.get(), 0, numMatched);
                    streamRowMatchCount += numMatched;
                    streamRowActive = (probed[0] != nullptr);
                    shipped = true;
                } else {
                    // Chain exhausted with no (more) matches for this row.
                    streamRowActive = false;
                }
            } else {
                streamRowActive = false;
            }
            if (shipped) {
                return true;
            }
            // Row fully processed. For a left join, emit a null-padded row if it never matched.
            if (streamLeftJoin && streamRowMatchCount == 0) {
                if (streamBuildState != nullptr) {
                    streamBuildState->initOriginalAndSelectedSize(1);
                }
                for (auto* v : streamBuildOutVecs) {
                    v->setNull(0, true);
                }
                streamRowMatchCount = 1; // guard against re-padding; advance on the next call
                return true;
            }
            // Otherwise fall through to load the next probe row.
        }
        // (2) Ensure a partition with unscanned probe rows is loaded.
        if (!streamHasPartition || streamProbeRowIdx >= streamProbeNumRows) {
            if (!streamLoadNextPartition()) {
                return false;
            }
        }
        // (3) Load the next probe row and probe the current partition's hash table. A NULL join key
        // never matches, so skip probing it (a LEFT join still null-pads via streamRowMatchCount == 0).
        streamLoadProbeRow();
        streamProbedTuples[0] = nullptr;
        bool keyIsNull = false;
        for (auto i = 0u; i < numKeys; i++) {
            if (streamProbeKeyVecs[i]->isNull(0)) {
                keyIsNull = true;
                break;
            }
        }
        if (!keyIsNull && streamJHT->getNumEntries() > 0) {
            streamJHT->probe(streamProbeKeyVecs, *streamHashVec, *streamHashSelVec,
                numKeys > 1 ? streamTmpHashVec.get() : nullptr, streamProbedTuples.get());
        }
        streamRowActive = true;
        streamRowMatchCount = 0;
    }
}

} // namespace processor
} // namespace kuzu
