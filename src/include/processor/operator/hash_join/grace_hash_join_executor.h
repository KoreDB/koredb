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
class DataChunkState;
class SelectionVector;
} // namespace common
namespace storage {
class MemoryManager;
} // namespace storage

namespace processor {

class JoinHashTable;

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

    // Materialize the full join result. Columns: [probeKeys..., probePayloads...,
    // buildPayloads...]. For a LEFT join, a probe row with no match emits one row with null build
    // payloads. Consumes both sides (partitions are freed as they are processed).
    std::unique_ptr<FactorizedTable> computeInnerJoin() {
        return computeJoin(false /*isLeftJoin*/);
    }
    std::unique_ptr<FactorizedTable> computeLeftJoin() { return computeJoin(true /*isLeftJoin*/); }

    // --- Streaming (resumable) probe -----------------------------------------------------------
    // Produce the join result one factorized chunk at a time instead of materializing it whole, so
    // a huge output flows in DEFAULT_VECTOR_CAPACITY-sized pieces. Each chunk mirrors the in-memory
    // HashJoinProbe's output shape: the probe columns are flat (one row) and the build-payload
    // columns are unflat (that row's matched build tuples). This is what the live HASH_JOIN operator
    // uses so that downstream sees exactly the same factorized structure as the in-memory path.
    //
    // probeOutVecs are the probe-side output columns in partition order [keys..., probePayloads...]
    // and MUST be flat (single-value state). buildOutVecs are the build-payload output columns and
    // MUST share one unflat state. getNextChunk() fills these in place and returns false when the
    // whole join has been consumed. Consumes both sides (partitions are freed as they are drained).
    void initProbeStream(std::vector<common::ValueVector*> probeOutVecs,
        std::vector<common::ValueVector*> buildOutVecs, bool isLeftJoin);
    bool getNextChunk();

    common::idx_t getNumPartitions() const { return buildParts.getNumPartitions(); }

private:
    std::unique_ptr<FactorizedTable> computeJoin(bool isLeftJoin);
    // Build a JoinHashTable from the (reloaded) build partition p. Empty partition -> empty table.
    std::unique_ptr<JoinHashTable> buildHashTableForPartition(common::idx_t p);
    // Streaming helpers.
    bool streamLoadNextPartition(); // free current, reload the next partition with probe rows
    void streamLoadProbeRow();      // scan the next probe row into the flat probe output vectors
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

    // --- Streaming probe state (valid between initProbeStream and the final getNextChunk). ---
    bool streamInitialized = false;
    bool streamLeftJoin = false;
    std::vector<common::ValueVector*> streamProbeOutVecs; // [keys..., probePayloads...], flat
    std::vector<common::ValueVector*> streamProbeKeyVecs; // first numKeys of streamProbeOutVecs
    std::vector<common::ValueVector*> streamBuildOutVecs; // build payloads, unflat (shared state)
    common::DataChunkState* streamBuildState = nullptr;
    std::vector<uint32_t> streamBuildColIdxs;
    // Partition iteration.
    common::idx_t streamNextPartition = 0; // index of the next partition to load
    bool streamHasPartition = false;
    common::idx_t streamCurPartition = 0;
    std::unique_ptr<JoinHashTable> streamJHT;
    FactorizedTable* streamProbePart = nullptr; // resident reloaded probe partition (owned elsewhere)
    uint64_t streamProbeNumRows = 0;
    uint64_t streamProbeRowIdx = 0; // next probe row (within the partition) to load
    // Block buffer for scanning the probe partition a vector at a time.
    std::shared_ptr<common::DataChunkState> streamProbeScanState;
    std::vector<std::unique_ptr<common::ValueVector>> streamProbeScanHolders;
    std::vector<common::ValueVector*> streamProbeScanVecs;
    uint64_t streamBlockSize = 0;   // rows currently in the scan buffer
    uint64_t streamBlockOffset = 0; // next row within the scan buffer
    // Current probe row's match-chain walk.
    bool streamRowActive = false;
    uint64_t streamRowMatchCount = 0;
    // Probe scratch.
    std::unique_ptr<common::ValueVector> streamHashVec;
    std::unique_ptr<common::ValueVector> streamTmpHashVec;
    std::unique_ptr<common::SelectionVector> streamHashSelVec;
    std::unique_ptr<uint8_t*[]> streamProbedTuples;
    std::unique_ptr<uint8_t*[]> streamMatchedTuples;
};

} // namespace processor
} // namespace kuzu
