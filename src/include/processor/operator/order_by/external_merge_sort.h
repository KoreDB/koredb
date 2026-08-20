#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/copy_constructors.h"
#include "processor/operator/order_by/order_by_data_info.h"

namespace koredb {
namespace common {
class Value;
class ValueVector;
class VirtualFileSystem;
struct FileInfo;
class BufferedFileWriter;
class BufferedFileReader;
class Deserializer;
} // namespace common
namespace storage {
class MemoryManager;
} // namespace storage

namespace processor {

// Out-of-core (external merge) sort. It reuses OrderByKeyEncoder to turn the ORDER BY keys of each
// tuple into a memcmp-comparable byte string, buffers (encoded-key, payload) records up to a memory
// budget, sorts each buffered run in memory and spills it to a single scratch file, then streams a
// k-way merge of the sorted runs back out in sorted order. Memory is bounded to ~the budget during
// generation and ~one buffered page per run during the merge, so a large ORDER BY no longer needs the
// whole result set resident at once (unlike the in-memory SortSharedState::payloadTables path).
//
// Keys are compared on the memcmp-comparable encoded prefix; STRING keys, whose encoding only holds a
// 12-byte prefix, are resolved column-by-column against the full string captured in the payload (see
// compareRecords), producing the strict total order over all key columns -- matching the in-memory
// RadixSort's full-key ordering. Nested keys are still excluded (the live operator gates on this).
//
// Multi-threaded ORDER BY: each thread owns its own ExternalMergeSort as a *run generator* (append +
// finalize -> sorted runs in its own spill file, lock-free during generation). After the barrier, one
// coordinator instance is handed every generator's runs (as file-scoped RunSources) via setMergeSources
// and streams a single k-way merge across all of them on the single scan thread.
class ExternalMergeSort {
public:
    // A sorted run addressed by the file it lives in: [fileOffset, ...) holding `numTuples` records.
    // Used to merge runs produced by *other* generator instances (each owns a different spill file).
    struct RunSource {
        common::FileInfo* file;
        uint64_t fileOffset;
        uint64_t numTuples;
    };

    ExternalMergeSort(const OrderByDataInfo& info, storage::MemoryManager* mm,
        common::VirtualFileSystem* vfs, std::string spillFilePath, uint64_t memBudgetBytes);
    ~ExternalMergeSort();
    DELETE_COPY_AND_MOVE(ExternalMergeSort);

    // Encodes the keys and captures the payload of every selected tuple in the input, appending them
    // to the current in-memory run. When the run exceeds the memory budget it is sorted and spilled.
    void append(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& payloadVectors);

    // Seals the last in-memory run (sorts + spills it). After this, scanNext() yields sorted tuples.
    void finalize();

    // Flush this generator's writer and append its sorted runs (as file-scoped RunSources) to `out`.
    void collectRunSources(std::vector<RunSource>& out);
    // Configure this instance as the merge coordinator over `sources` gathered from all generators.
    // scanNext() then streams a k-way merge across those runs (which may live in other spill files).
    void setMergeSources(std::vector<RunSource> sources);

    // Scans up to capacity sorted tuples into payloadVectors (which must be unflat and share one
    // DataChunkState). Returns the number of tuples produced; 0 once the merge is exhausted.
    uint64_t scanNext(const std::vector<common::ValueVector*>& payloadVectors);

    // Total tuples this instance will emit from scanNext: the merged total when it is a coordinator,
    // otherwise its own generated count (standalone/library use).
    uint64_t getNumTuples() const { return isCoordinator ? mergeTotalTuples : numTuples; }
    // Number of sorted runs produced by this generator (for tests: >1 means spilling actually happened).
    uint64_t getNumRuns() const { return runs.size(); }

private:
    // One spilled sorted run: a contiguous [fileOffset, fileOffset+byteSize) region of the scratch
    // file holding `numTuples` records, each `[numKeyBytes raw key][payload columns via Value]`.
    struct Run {
        uint64_t fileOffset;
        uint64_t numTuples;
    };
    // An in-memory record awaiting sort/spill.
    struct InMemRecord {
        std::vector<uint8_t> key; // numKeyBytes of memcmp-comparable encoded key
        std::vector<std::shared_ptr<common::Value>> payload;
    };
    // A read cursor over one spilled run during the merge; holds the run's current head record.
    struct MergeCursor {
        std::unique_ptr<common::Deserializer> deser; // owns the run's BufferedFileReader
        uint64_t remaining = 0;
        std::vector<uint8_t> headKey;
        std::vector<std::shared_ptr<common::Value>> headPayload;
        bool valid = false;
    };

    // A STRING key column: its byte offset within the encoded key, its sort direction, and the index
    // of the column in the payload where the full string lives (used to resolve prefix ties).
    struct StrKeyCol {
        uint32_t offsetInEncodedKey;
        bool isAsc;
        uint32_t payloadColIdx;
    };

    void sealRun();
    common::FileInfo* getOrCreateFile();
    void initMerge();
    void advanceCursor(MergeCursor& cursor);
    // 3-way compare (<0 / 0 / >0) of two records by the ORDER BY key. Plain memcmp of the encoded key
    // when there are no STRING keys; otherwise column-by-column with full-string tie resolution,
    // continuing past each string column so the order is strict over all key columns.
    int compareRecords(const uint8_t* keyA,
        const std::vector<std::shared_ptr<common::Value>>& payloadA, const uint8_t* keyB,
        const std::vector<std::shared_ptr<common::Value>>& payloadB) const;
    // true iff cursor a's head sorts strictly after b's (drives a min-key heap on the STL max-heap).
    bool keyGreater(uint32_t a, uint32_t b) const;

private:
    const OrderByDataInfo* info;
    storage::MemoryManager* mm;
    common::VirtualFileSystem* vfs;
    std::string spillFilePath;
    uint64_t memBudgetBytes;

    uint32_t numBytesPerTuple; // encoded key bytes + 8-byte (ignored here) payload back-pointer
    uint32_t numKeyBytes;      // numBytesPerTuple - 8
    uint32_t numPayloadCols;
    std::vector<StrKeyCol> strKeyCols; // STRING key columns, in key order (empty -> pure memcmp)

    std::vector<InMemRecord> buffer; // current (unsorted) in-memory run
    uint64_t bufferBytes = 0;

    std::vector<Run> runs;             // this generator's own runs, in its own spill file
    std::unique_ptr<common::FileInfo> fileInfo;
    std::shared_ptr<common::BufferedFileWriter> writer;
    uint64_t fileWriteOffset = 0;
    uint64_t numTuples = 0;            // tuples generated by this instance

    // Merge (coordinator / standalone) state.
    bool isCoordinator = false;         // set by setMergeSources; picks getNumTuples() semantics
    std::vector<RunSource> mergeSources; // runs to merge (own runs standalone, or all generators')
    uint64_t mergeTotalTuples = 0;       // sum of mergeSources' numTuples
    bool mergeInitialized = false;
    std::vector<std::unique_ptr<MergeCursor>> cursors;
    std::vector<uint32_t> heap; // min-heap of cursor indices, ordered by head key

    // Output emission shape, computed once on the first scanNext from the output vectors' states.
    bool outputModeComputed = false;
    bool outputBatchMode = false; // true: one shared unflat output state -> emit a full vector at once
};

// Test-only: number of times an ORDER BY has activated the external merge sort path this process.
uint64_t getExternalMergeSortActivationCount();

} // namespace processor
} // namespace koredb
