#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/api.h"
#include "common/types/types.h"
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

// An out-of-core (Grace) inner hash join over two inputs that may not fit in memory. Both the build
// and probe sides are radix-partitioned by hash(join keys) into the same number of partitions and
// spilled to disk under a memory budget, so that the two sides are co-partitioned: a probe key can
// only match build keys in the same partition. computeInnerJoin() then processes one partition at a
// time -- reload the build partition, build a JoinHashTable from it, reload and probe the matching
// probe partition, and free both -- so peak memory is bounded to roughly a single partition pair
// plus the output, instead of the whole build side.
//
// This wraps PartitionedFactorizedTable + the engine's JoinHashTable; it is the reusable core an
// out-of-core HASH_JOIN operator can delegate to. Output is materialized into a FactorizedTable
// with columns [probeKeys..., probePayloads..., buildPayloads...] (keys are taken from the probe
// side).
//
// Scope: inner join, flat (fixed-layout and variable-length via serialization) columns. Not
// thread-safe; intended to be driven by a single thread (or one instance per thread then merged).
class KUZU_API GraceHashJoinExecutor {
public:
    GraceHashJoinExecutor(storage::MemoryManager* mm, common::VirtualFileSystem* vfs,
        std::string buildSpillPath, std::string probeSpillPath,
        std::vector<common::LogicalType> keyTypes,
        std::vector<common::LogicalType> buildPayloadTypes,
        std::vector<common::LogicalType> probePayloadTypes, common::idx_t logNumPartitions,
        uint64_t memoryBudgetBytes);

    // Accumulate build/probe input. keyVectors and payloadVectors must share one unflat
    // DataChunkState. Rows are scattered by hash(keys) into partitions and spilled under the
    // budget.
    void appendBuild(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& payloadVectors);
    void appendProbe(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& payloadVectors);

    // Materialize the full inner-join result. Columns: [probeKeys..., probePayloads...,
    // buildPayloads...]. Consumes both sides (partitions are freed as they are processed).
    std::unique_ptr<FactorizedTable> computeInnerJoin();

    common::idx_t getNumPartitions() const { return buildParts.getNumPartitions(); }

private:
    void appendToPartitions(PartitionedFactorizedTable& parts,
        const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& payloadVectors);
    FactorizedTableSchema makeJoinHashTableSchema() const;
    FactorizedTableSchema makeOutputSchema() const;

private:
    storage::MemoryManager* mm;
    std::vector<common::LogicalType> keyTypes;
    std::vector<common::LogicalType> buildPayloadTypes;
    std::vector<common::LogicalType> probePayloadTypes;
    common::idx_t numKeys;
    uint64_t memoryBudgetBytes;

    PartitionedFactorizedTable buildParts; // schema [keys..., buildPayloads...]
    PartitionedFactorizedTable probeParts; // schema [keys..., probePayloads...]

    // Scratch vectors for computing routing hashes during append.
    std::unique_ptr<common::ValueVector> hashVector;
    std::unique_ptr<common::ValueVector> tmpHashVector;
};

} // namespace processor
} // namespace kuzu
