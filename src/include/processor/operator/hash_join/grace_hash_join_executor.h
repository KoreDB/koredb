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

    // Factorized build variant (the RETURN *-style factorized build shape): the build side delivers a
    // flat join key (or composite flat keys) plus one unflat payload group (N values sharing one
    // state) per call. The whole group is routed to the single partition hash(keys) selects and
    // flattened into N flat build rows -- the partition storage stays flat and the unflat OUTPUT is
    // reconstructed at probe time. keyVectors must be flat; payloadVectors (at least one) must share
    // one unflat state. Co-partitioned with appendProbe (same key hash).
    void appendBuildFactorized(const std::vector<common::ValueVector*>& keyVectors,
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

    // The kind of join the flat stream materializes per partition. The output column layout differs:
    // INNER/LEFT read build payloads (LEFT null-pads a non-match); COUNT reads the single pre-aggregated
    // count payload (0 on a non-match); MARK appends a BOOL mark (no build payloads).
    enum class FlatStreamMode { INNER, LEFT, MARK, COUNT };

    // --- Flat (row-at-a-time) streaming for arbitrary multi-chunk output -------------------------
    // Emit the join one FLAT tuple at a time into `outVecs` (order [probeKeys..., probePayloads...,
    // <buildPayloads | count | mark>]), setting each vector's state to a single value per call. Unlike
    // the factorized getNextChunk (probe-flat / build-unflat, which assumes those two live in separate
    // chunks), this is correct for ANY output chunk structure: every output column gets exactly one
    // value, so a downstream cross-product over the output chunks yields exactly one row. It is what the
    // live operator uses when the join output spans several data chunks (a factorized RETURN *-style
    // join, or a factorized outer for MARK/COUNT). Memory is bounded to one partition's materialized
    // output plus one partition pair (computed one partition at a time, scanned out row by row); slower
    // than a vectorized emission but never OOMs. `mode` selects which per-partition materialization runs.
    void initFlatStream(std::vector<common::ValueVector*> outVecs, FlatStreamMode mode);
    bool getNextFlatTuple();

    // Materialize just partition p's join output (columns [probeKeys..., probePayloads...,
    // buildPayloads...], flat) and free that partition pair. The single-chunk operator path calls this
    // per partition and scans each result into its one output chunk, so peak memory is bounded to one
    // partition's output instead of the whole join (which computeInnerJoin/computeLeftJoin materialize).
    std::unique_ptr<FactorizedTable> computePartitionJoin(common::idx_t p, bool isLeftJoin) {
        return computeJoin(isLeftJoin, p);
    }

    // --- MARK (EXISTS / semi-join) --------------------------------------------------------------
    // Emit exactly ONE output row per probe row -- columns [probeKeys..., probePayloads...,
    // mark(BOOL)] -- where mark is whether that probe row had >=1 build match. A NULL join key or an
    // empty build partition yields mark == false; no build payloads are read (a MARK join's build
    // side materializes only keys). Co-partitioning makes existence decidable one partition at a
    // time. computeMarkJoin() materializes the whole result; computeMarkPartition(p) restricts it to
    // partition p and frees that pair, bounding peak memory for the streaming operator path.
    std::unique_ptr<FactorizedTable> computeMarkJoin() { return computeMarkJoinImpl(ALL_PARTITIONS); }
    std::unique_ptr<FactorizedTable> computeMarkPartition(common::idx_t p) {
        return computeMarkJoinImpl(p);
    }

    // --- COUNT (size(...) / COUNT{} subquery) ---------------------------------------------------
    // The build side of a COUNT join is pre-aggregated to one (key, count) row per key, so each probe
    // row has at most one match. Emit exactly one output row per probe row -- [probeKeys...,
    // probePayloads...(the count column)] -- reading the matched key's count, or 0 when the key is
    // absent (a NULL join key counts as absent). This is LEFT-join-shaped (every probe row survives),
    // differing only in that a non-match yields count 0 instead of a NULL build payload. Single-chunk
    // output. computeCountPartition(p) restricts it to one partition (and frees the pair), bounding peak
    // memory for the streaming operator path.
    std::unique_ptr<FactorizedTable> computeCountJoin() {
        return computeJoin(false /*isLeftJoin*/, ALL_PARTITIONS, true /*countJoin*/);
    }
    std::unique_ptr<FactorizedTable> computeCountPartition(common::idx_t p) {
        return computeJoin(false /*isLeftJoin*/, p, true /*countJoin*/);
    }

    common::idx_t getNumPartitions() const { return buildParts.getNumPartitions(); }

private:
    // Sentinel for computeJoin's onlyPartition: process every partition (the materialized whole-join
    // path); any real partition index restricts it to that single partition (the flat-stream path).
    static constexpr common::idx_t ALL_PARTITIONS = ~static_cast<common::idx_t>(0);
    // Compute the join into a fresh FactorizedTable. With onlyPartition == ALL_PARTITIONS the whole
    // join is materialized; otherwise only that partition's output is produced (and that partition
    // pair freed), bounding peak memory for the flat-stream path.
    // countJoin: a probe row with no build match emits its single build payload as the integer 0
    // (COUNT semantics) instead of a NULL (LEFT semantics); like a left join, every probe row survives.
    std::unique_ptr<FactorizedTable> computeJoin(bool isLeftJoin,
        common::idx_t onlyPartition = ALL_PARTITIONS, bool countJoin = false);
    // Shared implementation behind computeMarkJoin()/computeMarkPartition(). onlyPartition ==
    // ALL_PARTITIONS materializes the whole MARK result; a real index restricts it to that partition.
    std::unique_ptr<FactorizedTable> computeMarkJoinImpl(common::idx_t onlyPartition);
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

    // --- Flat (row-at-a-time) streaming state (valid between initFlatStream and the final call). ---
    std::vector<common::ValueVector*> streamFlatOutVecs; // [keys..., probePayloads..., build/count/mark]
    FlatStreamMode streamFlatMode = FlatStreamMode::INNER;
    common::idx_t streamFlatPartition = 0;           // next partition to materialize
    std::unique_ptr<FactorizedTable> streamFlatTable; // current partition's materialized output
    uint64_t streamFlatCursor = 0;                    // next tuple within streamFlatTable to emit
};

} // namespace processor
} // namespace kuzu
