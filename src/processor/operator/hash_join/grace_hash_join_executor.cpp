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
    parts.appendVectors(allVectors, *hashVector);
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

std::unique_ptr<FactorizedTable> GraceHashJoinExecutor::computeInnerJoin() {
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
        auto jht =
            std::make_unique<JoinHashTable>(*mm, copyTypes(keyTypes), makeJoinHashTableSchema());
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
                const auto m =
                    std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, buildPart.getNumTuples() - t);
                scanState->initOriginalAndSelectedSize(m);
                buildPart.scan(std::span<ValueVector*>(bAllVecs), t, m);
                jht->appendVectors(bKeyVecs, bPayVecs, scanState.get());
            }
            jht->allocateHashSlots(jht->getNumEntries());
            jht->buildHashSlots();
        }

        auto& probePart = probeParts.getResidentPartition(p);
        if (probePart.getNumTuples() > 0 && jht->getNumEntries() > 0) {
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
                    for (auto i = 0u; i < numKeys; i++) {
                        probeKeyVecs[i]->copyFromVectorData(0, pKeyVecs[i], r);
                    }
                    for (auto i = 0u; i < probePayloadVecs.size(); i++) {
                        probePayloadVecs[i]->copyFromVectorData(0, pPayVecs[i], r);
                    }
                    flatState->getSelVectorUnsafe().setToUnfiltered(1);
                    probedTuples[0] = nullptr;
                    jht->probe(probeKeyVecs, probeHashVec, hashSelVec,
                        numKeys > 1 ? &probeTmpHashVec : nullptr, probedTuples.get());
                    while (probedTuples[0] != nullptr) {
                        const auto numMatched = jht->matchFlatKeys(probeKeyVecs, probedTuples.get(),
                            matchedTuples.get());
                        if (numMatched > 0) {
                            buildOutState->initOriginalAndSelectedSize(numMatched);
                            jht->lookup(buildPayloadVecs, buildPayloadColIdxs, matchedTuples.get(),
                                0, numMatched);
                            output->append(outputVecs);
                        }
                        if (numMatched < DEFAULT_VECTOR_CAPACITY) {
                            break;
                        }
                    }
                }
            }
        }
        buildParts.freePartition(p);
        probeParts.freePartition(p);
    }
    return output;
}

} // namespace processor
} // namespace kuzu
