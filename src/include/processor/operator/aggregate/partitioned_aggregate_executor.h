#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/api.h"
#include "common/types/types.h"
#include "function/aggregate_function.h"
#include "processor/result/partitioned_factorized_table.h"

namespace kuzu {
namespace common {
class ValueVector;
class VirtualFileSystem;
} // namespace common
namespace storage {
class MemoryManager;
} // namespace storage

namespace processor {

// An out-of-core (partitioned) hash aggregation over an input that may not fit in memory.
//
// Idea: the raw group-by input rows are radix-partitioned by hash(group keys) into
// 2^logNumPartitions partitions and spilled to disk under a memory budget. Because rows sharing a
// group key hash to the same partition, every group lives entirely within one partition, so the
// partitions are aggregated *independently* -- reload a partition, run a fresh AggregateHashTable
// over its rows, emit the finalized [keys..., aggResults...], free it -- and their results are
// simply concatenated with no cross-partition merge. Peak memory is bounded to roughly a single
// partition's rows plus its group table, instead of a table over the whole (distinct) group set.
//
// Why this avoids the hard problem: only *raw input columns* are ever written to disk (via
// FactorizedTable::serialize, which relocates variable-length/overflow data correctly). Aggregate
// *states* -- which for min/max(STRING), collect(LIST), etc. carry overflow pointers that are
// expensive to serialize -- never leave memory, because each partition is aggregated in one pass by
// an in-memory table. So this works for every aggregate function, including stateful ones.
//
// This wraps PartitionedFactorizedTable + the engine's AggregateHashTable; it is the reusable core a
// spilling HashAggregate operator can delegate to.
//
// Scope (v1): non-distinct aggregates; group keys stored flat and sharing one input state with the
// aggregate-input columns; ResultSet multiplicity of 1 (assert); no dependent (payload) keys. Not
// thread-safe; intended to be driven by a single thread.
class KUZU_API PartitionedAggregateExecutor {
public:
    // aggInputTypes[i] is the input column type of aggregate function i, or ANY() when the function
    // takes no input (e.g. COUNT(*)). aggResultTypes[i] is function i's finalized result type. Both
    // must be the same length as aggregateFunctions.
    PartitionedAggregateExecutor(storage::MemoryManager* mm, common::VirtualFileSystem* vfs,
        std::string spillPath, std::vector<common::LogicalType> keyTypes,
        std::vector<function::AggregateFunction> aggregateFunctions,
        std::vector<common::LogicalType> aggInputTypes,
        std::vector<common::LogicalType> aggResultTypes, common::idx_t logNumPartitions,
        uint64_t memoryBudgetBytes);

    // Accumulate input. keyVectors and the non-null entries of aggInputVectors must share one input
    // DataChunkState. aggInputVectors has one entry per aggregate function (nullptr for a function
    // that takes no input); its non-null vectors line up with the ANY() slots in aggInputTypes.
    // Rows are scattered by hash(keys) into partitions and spilled under the budget.
    void append(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& aggInputVectors);

    // Aggregate every partition and materialize the whole result. Columns: [keys..., aggResults...].
    // Consumes the input (partitions are freed as they are processed).
    std::unique_ptr<FactorizedTable> computeAggregates();

    common::idx_t getNumPartitions() const { return parts.getNumPartitions(); }

private:
    FactorizedTableSchema makeAggHashTableSchema() const;
    FactorizedTableSchema makeOutputSchema() const;
    // Build + fill an AggregateHashTable from the (reloaded) partition p, then finalize its states.
    // Returns nullptr for an empty partition.
    std::unique_ptr<class ReaggregatingHashTable> aggregatePartition(common::idx_t p);

private:
    storage::MemoryManager* mm;
    std::vector<common::LogicalType> keyTypes;
    std::vector<function::AggregateFunction> aggregateFunctions;
    std::vector<common::LogicalType> aggInputTypes;  // per function; ANY() == no input
    std::vector<common::LogicalType> aggResultTypes; // per function
    common::idx_t numKeys;
    uint64_t memoryBudgetBytes;
    // Partition-input column index for each aggregate function (after the key columns), or -1 for a
    // function with no input. Non-input functions store no column.
    std::vector<int32_t> funcInputCol;
    common::idx_t numInputCols;

    PartitionedFactorizedTable parts; // schema [keys..., aggInputCols...]

    std::unique_ptr<common::ValueVector> hashVector;
    std::unique_ptr<common::ValueVector> tmpHashVector;
};

} // namespace processor
} // namespace kuzu
