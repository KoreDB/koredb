#include "processor/operator/aggregate/partitioned_aggregate_executor.h"

#include <algorithm>
#include <numeric>
#include <span>

#include "common/data_chunk/data_chunk_state.h"
#include "common/vector/value_vector.h"
#include "function/hash/vector_hash_functions.h"
#include "processor/operator/aggregate/aggregate_hash_table.h"
#include "processor/operator/aggregate/aggregate_input.h"
#include "processor/result/factorized_table_util.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;
using namespace kuzu::function;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

// A trivial AggregateHashTable subclass that only makes the protected append() reachable so this
// executor can drive a private per-partition table directly. Adds no state or behavior.
class ReaggregatingHashTable final : public AggregateHashTable {
public:
    using AggregateHashTable::AggregateHashTable;
    using AggregateHashTable::append;
};

static std::vector<LogicalType> copyTypes(const std::vector<LogicalType>& types) {
    std::vector<LogicalType> result;
    result.reserve(types.size());
    for (auto& t : types) {
        result.push_back(t.copy());
    }
    return result;
}

// Data columns spilled per partition: [keys..., <one column per aggregate function that has an
// input>]. Functions with no input (COUNT(*), whose type is ANY()) contribute no column.
static std::vector<LogicalType> makePartitionColumnTypes(const std::vector<LogicalType>& keyTypes,
    const std::vector<LogicalType>& aggInputTypes) {
    auto result = copyTypes(keyTypes);
    for (auto& t : aggInputTypes) {
        if (t.getLogicalTypeID() != LogicalTypeID::ANY) {
            result.push_back(t.copy());
        }
    }
    return result;
}

PartitionedAggregateExecutor::PartitionedAggregateExecutor(MemoryManager* mm, VirtualFileSystem* vfs,
    std::string spillPath, std::vector<LogicalType> keyTypes,
    std::vector<AggregateFunction> aggregateFunctions, std::vector<LogicalType> aggInputTypes,
    std::vector<LogicalType> aggResultTypes, idx_t logNumPartitions, uint64_t memoryBudgetBytes)
    : mm{mm}, keyTypes{std::move(keyTypes)}, aggregateFunctions{std::move(aggregateFunctions)},
      aggInputTypes{std::move(aggInputTypes)}, aggResultTypes{std::move(aggResultTypes)},
      numKeys{static_cast<idx_t>(this->keyTypes.size())}, memoryBudgetBytes{memoryBudgetBytes},
      parts{mm, makePartitionColumnTypes(this->keyTypes, this->aggInputTypes), logNumPartitions, vfs,
          std::move(spillPath)},
      hashVector{std::make_unique<ValueVector>(LogicalType::HASH(), mm)},
      tmpHashVector{std::make_unique<ValueVector>(LogicalType::HASH(), mm)} {
    // Map each aggregate function to its input column index within the partition (after the keys),
    // or -1 for a function with no input.
    numInputCols = 0;
    funcInputCol.reserve(this->aggInputTypes.size());
    for (auto& t : this->aggInputTypes) {
        if (t.getLogicalTypeID() != LogicalTypeID::ANY) {
            funcInputCol.push_back(static_cast<int32_t>(numInputCols++));
        } else {
            funcInputCol.push_back(-1);
        }
    }
}

FactorizedTableSchema PartitionedAggregateExecutor::makeAggHashTableSchema() const {
    // Mirrors map_aggregate's getFactorizedTableSchema: [keys (flat), aggStates..., hash].
    FactorizedTableSchema schema;
    for (auto& t : keyTypes) {
        schema.appendColumn(
            ColumnSchema(false /*isUnFlat*/, 0 /*groupID*/, LogicalTypeUtils::getRowLayoutSize(t)));
    }
    for (auto& func : aggregateFunctions) {
        schema.appendColumn(ColumnSchema(false, 0, func.getAggregateStateSize()));
    }
    schema.appendColumn(ColumnSchema(false, 0, sizeof(hash_t)));
    return schema;
}

FactorizedTableSchema PartitionedAggregateExecutor::makeOutputSchema() const {
    auto types = copyTypes(keyTypes);
    for (auto& t : aggResultTypes) {
        types.push_back(t.copy());
    }
    return FactorizedTableUtils::createFlatTableSchema(std::move(types));
}

void PartitionedAggregateExecutor::append(const std::vector<ValueVector*>& keyVectors,
    const std::vector<ValueVector*>& aggInputVectors) {
    KU_ASSERT(keyVectors.size() == numKeys);
    KU_ASSERT(aggInputVectors.size() == aggregateFunctions.size());
    // Routing hash over the group keys only, so rows of the same group co-locate in one partition.
    // This is the same hash the in-partition AggregateHashTable computes over the keys.
    auto* state = keyVectors[0]->state.get();
    auto& sel = state->getSelVector();
    VectorHashFunction::computeHash(*keyVectors[0], sel, *hashVector, sel);
    for (auto i = 1u; i < keyVectors.size(); i++) {
        VectorHashFunction::computeHash(*keyVectors[i], sel, *tmpHashVector, sel);
        VectorHashFunction::combineHash(*hashVector, sel, *tmpHashVector, sel, *hashVector, sel);
    }
    std::vector<ValueVector*> allVectors;
    allVectors.reserve(numKeys + numInputCols);
    allVectors.insert(allVectors.end(), keyVectors.begin(), keyVectors.end());
    for (auto f = 0u; f < aggInputVectors.size(); f++) {
        if (funcInputCol[f] >= 0) {
            KU_ASSERT(aggInputVectors[f] != nullptr);
            allVectors.push_back(aggInputVectors[f]);
        }
    }
    parts.appendVectors(allVectors, *hashVector);
    parts.spillToReduceResidentBytesTo(memoryBudgetBytes);
}

std::unique_ptr<ReaggregatingHashTable> PartitionedAggregateExecutor::aggregatePartition(idx_t p) {
    auto& part = parts.getResidentPartition(p);
    if (part.getNumTuples() == 0) {
        return nullptr;
    }
    std::vector<LogicalType> distinctAggKeyTypes;
    distinctAggKeyTypes.reserve(aggregateFunctions.size());
    for (auto i = 0u; i < aggregateFunctions.size(); i++) {
        distinctAggKeyTypes.push_back(LogicalType::ANY()); // v1: no distinct aggregates
    }
    auto aggHT = std::make_unique<ReaggregatingHashTable>(*mm, copyTypes(keyTypes),
        std::vector<LogicalType>{} /*dependentKeyTypes*/, aggregateFunctions, distinctAggKeyTypes,
        0 /*numEntriesToAllocate*/, makeAggHashTableSchema());

    // Scan the partition a vector at a time and re-aggregate. The scan vectors are [keys...,
    // aggInputCols...], the same column order stored in the partition.
    auto scanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    std::vector<std::unique_ptr<ValueVector>> holders;
    std::vector<ValueVector*> keyScanVecs, inputScanVecs, allScanVecs;
    for (auto& t : keyTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = scanState;
        keyScanVecs.push_back(v.get());
        allScanVecs.push_back(v.get());
        holders.push_back(std::move(v));
    }
    for (auto& t : aggInputTypes) {
        if (t.getLogicalTypeID() == LogicalTypeID::ANY) {
            continue;
        }
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = scanState;
        inputScanVecs.push_back(v.get());
        allScanVecs.push_back(v.get());
        holders.push_back(std::move(v));
    }
    for (uint64_t t = 0; t < part.getNumTuples(); t += DEFAULT_VECTOR_CAPACITY) {
        const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, part.getNumTuples() - t);
        scanState->initOriginalAndSelectedSize(m);
        part.scan(std::span<ValueVector*>(allScanVecs), t, m);
        std::vector<AggregateInput> aggInputs(aggregateFunctions.size());
        for (auto f = 0u; f < aggregateFunctions.size(); f++) {
            if (funcInputCol[f] >= 0) {
                aggInputs[f].aggregateVector = inputScanVecs[funcInputCol[f]];
            }
        }
        aggHT->append(keyScanVecs, std::vector<ValueVector*>{} /*dependentKeyVectors*/,
            scanState.get(), aggInputs, 1 /*resultSetMultiplicity*/);
    }
    aggHT->finalizeAggregateStates();
    return aggHT;
}

std::unique_ptr<FactorizedTable> PartitionedAggregateExecutor::computeAggregates() {
    auto output = std::make_unique<FactorizedTable>(mm, makeOutputSchema());

    // Output vectors: [keys..., aggResults...], all unflat and sharing one state so a whole scanned
    // block of finalized groups is appended at once.
    auto outState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    std::vector<std::unique_ptr<ValueVector>> outHolders;
    std::vector<ValueVector*> outKeyVecs, outAggVecs, outputVecs;
    for (auto& t : keyTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = outState;
        outKeyVecs.push_back(v.get());
        outputVecs.push_back(v.get());
        outHolders.push_back(std::move(v));
    }
    for (auto& t : aggResultTypes) {
        auto v = std::make_unique<ValueVector>(t.copy(), mm);
        v->state = outState;
        outAggVecs.push_back(v.get());
        outputVecs.push_back(v.get());
        outHolders.push_back(std::move(v));
    }
    std::vector<uint32_t> keyColIdxs(numKeys);
    std::iota(keyColIdxs.begin(), keyColIdxs.end(), 0u);
    auto entries = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);

    for (idx_t p = 0; p < parts.getNumPartitions(); p++) {
        auto aggHT = aggregatePartition(p);
        parts.freePartition(p); // raw rows already copied into aggHT
        if (aggHT == nullptr) {
            continue;
        }
        const auto* ft = aggHT->getFactorizedTable();
        const auto numGroups = aggHT->getNumEntries();
        const auto aggStateColOffset = ft->getTableSchema()->getColOffset(numKeys);
        for (uint64_t g = 0; g < numGroups; g += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numGroups - g);
            for (uint64_t j = 0; j < m; j++) {
                entries[j] = ft->getTuple(g + j);
            }
            // Group keys: written into the (unflat) key output vectors; this sets outState's size.
            ft->lookup(outKeyVecs, keyColIdxs, entries.get(), 0, m);
            // Aggregate results: read each finalized state and write its value.
            for (uint64_t j = 0; j < m; j++) {
                auto* entry = entries[j];
                auto off = aggStateColOffset;
                for (auto f = 0u; f < aggregateFunctions.size(); f++) {
                    auto* aggState = reinterpret_cast<AggregateState*>(entry + off);
                    if (aggregateFunctions[f].needToHandleNulls) {
                        outAggVecs[f]->setNull(j, false);
                        aggState->writeToVector(outAggVecs[f], j);
                    } else {
                        const auto isNull = aggState->constCast<AggregateStateWithNull>().isNull;
                        outAggVecs[f]->setNull(j, isNull);
                        if (!isNull) {
                            aggState->writeToVector(outAggVecs[f], j);
                        }
                    }
                    off += aggregateFunctions[f].getAggregateStateSize();
                }
            }
            output->append(outputVecs);
        }
    }
    return output;
}

} // namespace processor
} // namespace kuzu
