#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/copy_constructors.h"
#include "processor/operator/order_by/order_by_data_info.h"

namespace kuzu {
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
// v1 scope: the encoded key must be a total order under plain memcmp, i.e. keys are fixed-width
// (non-STRING, non-nested) types. STRING keys only store a 12-byte prefix in the encoding, so two
// distinct long strings sharing that prefix would compare equal here; resolving such ties (against the
// full payload value) is a follow-up. The live operator gates on this restriction.
class ExternalMergeSort {
public:
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

    // Scans up to capacity sorted tuples into payloadVectors (which must be unflat and share one
    // DataChunkState). Returns the number of tuples produced; 0 once the merge is exhausted.
    uint64_t scanNext(const std::vector<common::ValueVector*>& payloadVectors);

    uint64_t getNumTuples() const { return numTuples; }
    // Number of sorted runs produced (for tests: >1 means spilling actually happened).
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

    void sealRun();
    common::FileInfo* getOrCreateFile();
    void initMerge();
    void advanceCursor(MergeCursor& cursor);
    // memcmp of the two cursors' head keys; true iff a's head key is strictly greater than b's (used
    // to drive a min-key heap on top of the STL max-heap primitives).
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

    std::vector<InMemRecord> buffer; // current (unsorted) in-memory run
    uint64_t bufferBytes = 0;

    std::vector<Run> runs;
    std::unique_ptr<common::FileInfo> fileInfo;
    std::shared_ptr<common::BufferedFileWriter> writer;
    uint64_t fileWriteOffset = 0;
    uint64_t numTuples = 0;

    bool mergeInitialized = false;
    std::vector<std::unique_ptr<MergeCursor>> cursors;
    std::vector<uint32_t> heap; // min-heap of cursor indices, ordered by head key
};

} // namespace processor
} // namespace kuzu
