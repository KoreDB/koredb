#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/api.h"
#include "common/types/types.h"
#include "function/aggregate_function.h"
#include "processor/result/partitioned_factorized_table.h"

namespace koredb {
namespace common {
class ValueVector;
class VirtualFileSystem;
} // namespace common
namespace storage {
class MemoryManager;
} // namespace storage

namespace processor {

class AggregateHashTable;

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
// Dependent (payload) keys are columns functionally dependent on the group keys (e.g. `n.name` when
// grouping by `n.id`): stored and emitted but not hashed/compared, exactly as the in-memory
// HashAggregate treats them.
//
// Scope (v1): non-distinct aggregates; group keys stored flat and sharing one input state with the
// dependent-key and aggregate-input columns. Not thread-safe; intended to be driven by a single
// thread. Per-row input multiplicity is supported (stored and re-applied).
class KOREDB_API PartitionedAggregateExecutor {
public:
    // aggInputTypes[i] is the input column type of aggregate function i, or ANY() when the function
    // takes no input (e.g. COUNT(*)). aggResultTypes[i] is function i's finalized result type. Both
    // must be the same length as aggregateFunctions. dependentKeyTypes are functionally-dependent
    // payload columns (may be empty).
    PartitionedAggregateExecutor(storage::MemoryManager* mm, common::VirtualFileSystem* vfs,
        std::string spillPath, std::vector<common::LogicalType> keyTypes,
        std::vector<common::LogicalType> dependentKeyTypes,
        std::vector<function::AggregateFunction> aggregateFunctions,
        std::vector<common::LogicalType> aggInputTypes,
        std::vector<common::LogicalType> aggResultTypes, common::idx_t logNumPartitions,
        uint64_t memoryBudgetBytes);

    // Accumulate input. keyVectors, dependentKeyVectors, and the non-null entries of aggInputVectors
    // must share one input DataChunkState. aggInputVectors has one entry per aggregate function
    // (nullptr for a function that takes no input); its non-null vectors line up with the ANY() slots
    // in aggInputTypes. `multiplicity` is the ResultSet multiplicity of this batch (times any
    // factorized sibling-chunk expansion) -- each row counts that many times; it is stored per row
    // and re-applied during aggregation. Rows are scattered by hash(keys) into partitions and spilled
    // under the budget.
    void append(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& dependentKeyVectors,
        const std::vector<common::ValueVector*>& aggInputVectors, uint64_t multiplicity = 1);

    // Aggregate every partition and materialize the whole result. Columns:
    // [keys..., dependentKeys..., aggResults...]. Consumes the input (partitions are freed as they are
    // processed).
    std::unique_ptr<FactorizedTable> computeAggregates();

    // Aggregate every partition into a finalized in-memory AggregateHashTable, one per non-empty
    // partition (each holds a disjoint set of groups). Consumes the input (partitions are freed as
    // they are processed). Lets a spilling HashAggregate operator reuse the normal aggregate-scan
    // path, which reads finalized states out of these tables. Peak memory is O(#groups) (as for the
    // in-memory path); the *input* side was bounded by spilling during append.
    std::vector<std::unique_ptr<AggregateHashTable>> finalizeToTables();

    // Merge another executor's accumulated input partitions into this one, partition by partition
    // (both must have the same schema and partition count). `other` is left empty. This is how a set
    // of per-thread executors is combined into one before finalizeToTables, mirroring how per-thread
    // JoinHashTables merge into a global one. Same-hash rows co-locate, so a group still lands in a
    // single merged partition. Not called concurrently with append on either side.
    void merge(PartitionedAggregateExecutor& other);

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
    std::vector<common::LogicalType> dependentKeyTypes;
    std::vector<function::AggregateFunction> aggregateFunctions;
    std::vector<common::LogicalType> aggInputTypes;  // per function; ANY() == no input
    std::vector<common::LogicalType> aggResultTypes; // per function
    common::idx_t numKeys;
    common::idx_t numDependentKeys;
    uint64_t memoryBudgetBytes;
    // Partition-input column index for each aggregate function (after the key + dependent-key
    // columns), or -1 for a function with no input. Non-input functions store no column.
    std::vector<int32_t> funcInputCol;
    common::idx_t numInputCols;
    // Scratch INT64 vector holding the per-row multiplicity for the current append batch.
    std::unique_ptr<common::ValueVector> multiplicityVector;

    PartitionedFactorizedTable parts; // schema [keys..., aggInputCols...]

    std::unique_ptr<common::ValueVector> hashVector;
    std::unique_ptr<common::ValueVector> tmpHashVector;
};

} // namespace processor
} // namespace koredb
