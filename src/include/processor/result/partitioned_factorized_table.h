#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/api.h"
#include "common/types/types.h"
#include "processor/result/factorized_table.h"

namespace kuzu {
namespace common {
class VirtualFileSystem;
struct FileInfo;
class ValueVector;
} // namespace common
namespace storage {
class MemoryManager;
} // namespace storage

namespace processor {

// A hash-radix-partitioned set of flat FactorizedTables. Build-side tuples are scattered into
// 2^logNumPartitions partitions by the high bits of their hash. Any partition can be spilled to a
// temp file (as a position-independent serialized byte range, via FactorizedTable::serialize) and
// later reloaded, which bounds the peak resident memory of a build side to a single partition
// instead of the whole input. This is the core building block for out-of-core (Grace) hash join and
// partitioned aggregation.
//
// Memory model: spilling a partition frees its DataBlocks, which the MemoryManager returns to its
// free-page pool for reuse by later allocations without a new buffer-manager reservation. So the
// guarantee is a *page-reuse* bound on the operator's own peak footprint, not a drop in the global
// used-memory counter. Spilling here is therefore operator-triggered (the caller decides when a
// partition is inactive), distinct from the buffer-manager-triggered raw-buffer spill used by
// ChunkedNodeGroup for position-independent columnar data.
//
// Threading: not thread-safe. Intended to be used per-thread and merged, mirroring how
// JoinHashTable local/global tables are used.
class KUZU_API PartitionedFactorizedTable {
public:
    PartitionedFactorizedTable(storage::MemoryManager* mm,
        std::vector<common::LogicalType> columnTypes, common::idx_t logNumPartitions,
        common::VirtualFileSystem* vfs, std::string tmpFilePath);
    ~PartitionedFactorizedTable();

    common::idx_t getNumPartitions() const { return partitions.size(); }
    common::idx_t getLogNumPartitions() const { return logNumPartitions; }

    // Partition index for a hash value: uses the high bits so it does not overlap with the low bits
    // an in-partition hash table indexes on.
    common::idx_t getPartitionIdxForHash(common::hash_t hash) const {
        return logNumPartitions == 0 ? 0 :
                                       static_cast<common::idx_t>(hash >> (64 - logNumPartitions));
    }

    // Scatter the selected rows of `vectors` into partitions by the hash in `hashVector`. All input
    // vectors (and hashVector) must share a single, unflat DataChunkState/selection vector. The
    // shared selection vector is temporarily overwritten during the scatter and restored on return.
    void appendVectors(const std::vector<common::ValueVector*>& vectors,
        const common::ValueVector& hashVector);

    // Number of (flat) tuples in a partition, whether resident or spilled.
    uint64_t getPartitionNumTuples(common::idx_t partitionIdx) const;
    uint64_t getNumTuples() const;

    // Approximate resident (in-memory) tuple bytes across non-spilled partitions. Counts only the
    // fixed per-tuple bytes (excludes variable-length overflow), so it is a cheap spill trigger,
    // not exact memory accounting.
    uint64_t getResidentTupleBytes() const;

    bool isSpilled(common::idx_t partitionIdx) const { return spillStates[partitionIdx].spilled; }

    // Returns the partition, reloading it from disk first if it was spilled.
    FactorizedTable& getResidentPartition(common::idx_t partitionIdx);

    // Merge another partitioned table into this one, partition by partition. Both must have the
    // same schema and number of partitions (they radix-partition identically). Used to combine
    // per-thread build tables into a global one. Spilled partitions on either side are reloaded
    // first, and `other`'s partitions are emptied.
    void merge(PartitionedFactorizedTable& other);

    // Serialize the partition to the temp file and free its in-memory blocks. No-op if already
    // spilled or empty.
    void spillPartition(common::idx_t partitionIdx);
    // Reload a spilled partition from disk. No-op if resident.
    void reloadPartition(common::idx_t partitionIdx);

    // Spill every resident, non-empty partition. Returns the number of partitions spilled.
    common::idx_t spillAllPartitions();
    // Spill the resident partition holding the most tuples. Returns its tuple count, or 0 if there
    // is no non-empty resident partition.
    uint64_t spillLargestResidentPartition();
    // Operator-triggered spill driver: spill the largest resident partitions until the resident
    // tuple bytes are at most maxResidentBytes (or no non-empty resident partition remains).
    // Returns the number of partitions spilled. The build side calls this to bound its own peak
    // footprint.
    common::idx_t spillToReduceResidentBytesTo(uint64_t maxResidentBytes);

private:
    common::FileInfo* getOrCreateFile();
    FactorizedTableSchema createSchema() const;

private:
    storage::MemoryManager* mm;
    std::vector<common::LogicalType> columnTypes;
    common::idx_t logNumPartitions;
    uint64_t numBytesPerTuple;
    std::vector<std::unique_ptr<FactorizedTable>> partitions;

    struct SpillState {
        bool spilled = false;
        uint64_t fileOffset = 0;
        uint64_t byteSize = 0;
        uint64_t numTuples = 0; // tuple count while spilled (resident count comes from the FT)
    };
    std::vector<SpillState> spillStates;

    common::VirtualFileSystem* vfs;
    std::string tmpFilePath;
    std::unique_ptr<common::FileInfo> fileInfo;
    uint64_t fileWriteOffset = 0;

    // Reusable scratch buffers for the scatter, sized to the number of partitions.
    std::vector<std::vector<common::sel_t>> scatterBuckets;
};

} // namespace processor
} // namespace kuzu
