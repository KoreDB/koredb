#include "processor/operator/order_by/external_merge_sort.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <system_error>

#include "common/constants.h"
#include "common/file_system/file_info.h"
#include "common/file_system/virtual_file_system.h"
#include "common/serializer/buffered_file.h"
#include "common/serializer/deserializer.h"
#include "common/serializer/serializer.h"
#include "common/types/types.h"
#include "common/types/value/value.h"
#include "common/vector/value_vector.h"
#include "processor/operator/order_by/order_by_key_encoder.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;

namespace kuzu {
namespace processor {

ExternalMergeSort::ExternalMergeSort(const OrderByDataInfo& info, storage::MemoryManager* mm,
    VirtualFileSystem* vfs, std::string spillFilePath, uint64_t memBudgetBytes)
    : info{&info}, mm{mm}, vfs{vfs}, spillFilePath{std::move(spillFilePath)},
      memBudgetBytes{memBudgetBytes} {
    // The encoded-key tuple is [encoded keys ...][8-byte payload back-pointer]. We only keep the
    // encoded-key prefix (memcmp-comparable); the back-pointer is unused by the stream format.
    uint32_t encodedKeyBytes = 0;
    for (auto& keyType : info.keyTypes) {
        encodedKeyBytes += OrderByKeyEncoder::getEncodingSize(keyType);
    }
    numBytesPerTuple = encodedKeyBytes + OrderByConstants::NUM_BYTES_FOR_PAYLOAD_IDX;
    numKeyBytes = encodedKeyBytes;
    numPayloadCols = info.payloadTypes.size();
    // Record each STRING key column's encoded-key offset and the payload column holding its full
    // value, so a prefix tie can be resolved against the full string during comparison.
    uint32_t encOffset = 0;
    for (auto k = 0u; k < info.keyTypes.size(); k++) {
        if (info.keyTypes[k].getPhysicalType() == common::PhysicalTypeID::STRING) {
            strKeyCols.push_back(StrKeyCol{encOffset, info.isAscOrder[k], info.keyInPayloadPos[k]});
        }
        encOffset += OrderByKeyEncoder::getEncodingSize(info.keyTypes[k]);
    }
}

int ExternalMergeSort::compareRecords(const uint8_t* keyA,
    const std::vector<std::shared_ptr<Value>>& payloadA, const uint8_t* keyB,
    const std::vector<std::shared_ptr<Value>>& payloadB) const {
    if (strKeyCols.empty()) {
        return std::memcmp(keyA, keyB, numKeyBytes);
    }
    // Column-by-column, resolving each STRING column's 12-byte-prefix tie against the full payload
    // string, then continuing to the remaining columns. This produces the STRICT total order over all
    // key columns -- matching the in-memory RadixSort, which sorts by the full encoded key (the
    // KeyBlockMerger's habit of ignoring columns after the last string is only valid on top of that
    // full-key pre-sort, so it must NOT be replicated for a standalone comparator). String prefix ties
    // for short strings are exact; for long strings they are resolved against the full payload string.
    const uint32_t strEncSize =
        OrderByKeyEncoder::getEncodingSize(LogicalType(LogicalTypeID::STRING));
    uint32_t lastComparedBytes = 0;
    for (auto& sc : strKeyCols) {
        // Compare the encoded bytes from the last resolved column up to and including this string's
        // prefix (covers any fixed-width key columns sitting between the two string columns).
        const auto cmpLen = sc.offsetInEncodedKey - lastComparedBytes + strEncSize;
        int result = std::memcmp(keyA + lastComparedBytes, keyB + lastComparedBytes, cmpLen);
        const auto* leftStrPtr = keyA + sc.offsetInEncodedKey;
        const auto* rightStrPtr = keyB + sc.offsetInEncodedKey;
        // Advance past this string column; a `continue` means it tied and we move to later columns.
        lastComparedBytes = sc.offsetInEncodedKey + strEncSize;
        if (OrderByKeyEncoder::isNullVal(leftStrPtr, sc.isAsc) &&
            OrderByKeyEncoder::isNullVal(rightStrPtr, sc.isAsc)) {
            continue;
        }
        if (result == 0) {
            const bool leftLong = OrderByKeyEncoder::isLongStr(leftStrPtr, sc.isAsc);
            const bool rightLong = OrderByKeyEncoder::isLongStr(rightStrPtr, sc.isAsc);
            if (!leftLong && !rightLong) {
                continue; // both fit in the prefix -> equal on this column, compare later columns
            } else if (leftLong && !rightLong) {
                return sc.isAsc ? 1 : -1;
            } else if (!leftLong && rightLong) {
                return sc.isAsc ? -1 : 1;
            }
            const std::string sA = payloadA[sc.payloadColIdx]->getValue<std::string>();
            const std::string sB = payloadB[sc.payloadColIdx]->getValue<std::string>();
            if (sA == sB) {
                continue; // equal full strings -> compare later columns
            }
            return (sc.isAsc == (sA > sB)) ? 1 : -1;
        }
        return result;
    }
    // Compare any remaining fixed-width key columns after the last string column.
    if (lastComparedBytes < numKeyBytes) {
        return std::memcmp(keyA + lastComparedBytes, keyB + lastComparedBytes,
            numKeyBytes - lastComparedBytes);
    }
    return 0;
}

ExternalMergeSort::~ExternalMergeSort() {
    // Release the readers/writer (they hold a FileInfo&) before removing the scratch file. The spill
    // file is a plain local temp file, removed directly rather than through
    // VirtualFileSystem::removeFileIfExists (which restricts removal to the database's own file set).
    // Cleanup is best-effort and must never throw from a destructor.
    writer.reset();
    cursors.clear();
    fileInfo.reset();
    if (!spillFilePath.empty()) {
        std::error_code errCode;
        std::filesystem::remove(spillFilePath, errCode);
    }
}

FileInfo* ExternalMergeSort::getOrCreateFile() {
    if (fileInfo == nullptr) {
        fileInfo = vfs->openFile(spillFilePath,
            FileOpenFlags(FileFlags::READ_ONLY | FileFlags::WRITE |
                          FileFlags::CREATE_AND_TRUNCATE_IF_EXISTS));
        writer = std::make_shared<BufferedFileWriter>(*fileInfo);
    }
    return fileInfo.get();
}

void ExternalMergeSort::append(const std::vector<ValueVector*>& keyVectors,
    const std::vector<ValueVector*>& payloadVectors) {
    if (keyVectors.empty()) {
        return;
    }
    auto& keyState = *keyVectors[0]->state;
    const auto numTuplesInBatch = keyState.getSelVector().getSelSize();
    if (numTuplesInBatch == 0) {
        return;
    }
    // Encode this batch's keys with a fresh encoder, then read each tuple's memcmp-comparable key
    // prefix back out of the encoder's key blocks (in selection order).
    OrderByKeyEncoder encoder(*info, mm, 0 /* ftIdx */, DEFAULT_VECTOR_CAPACITY, numBytesPerTuple);
    encoder.encodeKeys(keyVectors);
    std::vector<std::vector<uint8_t>> batchKeys;
    batchKeys.reserve(numTuplesInBatch);
    for (auto& block : encoder.getKeyBlocks()) {
        for (auto i = 0u; i < block->numTuples; i++) {
            const auto* tuplePtr = block->getData() + static_cast<uint64_t>(i) * numBytesPerTuple;
            batchKeys.emplace_back(tuplePtr, tuplePtr + numKeyBytes);
        }
    }
    KU_ASSERT(batchKeys.size() == numTuplesInBatch);
    for (auto t = 0u; t < numTuplesInBatch; t++) {
        InMemRecord rec;
        rec.key = std::move(batchKeys[t]);
        rec.payload.reserve(numPayloadCols);
        for (auto c = 0u; c < numPayloadCols; c++) {
            // Read each payload at its own position so keys and payloads may live in different (flat)
            // factorization groups: a flat vector holds one value broadcast to every tuple in the
            // batch, while an unflat vector is indexed per tuple (mirroring OrderByKeyEncoder).
            auto& st = *payloadVectors[c]->state;
            const auto pos = st.isFlat() ? st.getSelVector()[0] : st.getSelVector()[t];
            rec.payload.push_back(payloadVectors[c]->getAsValue(pos));
        }
        buffer.push_back(std::move(rec));
    }
    numTuples += numTuplesInBatch;
    // Estimate the resident bytes of the current run (exact for fixed-width payloads).
    uint64_t payloadRowEstimate = 0;
    for (auto& payloadType : info->payloadTypes) {
        payloadRowEstimate += LogicalTypeUtils::getRowLayoutSize(payloadType);
    }
    bufferBytes += static_cast<uint64_t>(numTuplesInBatch) * (numKeyBytes + payloadRowEstimate);
    if (bufferBytes >= memBudgetBytes && memBudgetBytes > 0) {
        sealRun();
    }
}

void ExternalMergeSort::sealRun() {
    if (buffer.empty()) {
        return;
    }
    // Sort the run in memory by the ORDER BY key (memcmp for fixed-width keys, string-aware otherwise).
    std::vector<uint32_t> order(buffer.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) {
        return compareRecords(buffer[a].key.data(), buffer[a].payload, buffer[b].key.data(),
                   buffer[b].payload) < 0;
    });
    getOrCreateFile();
    const auto runStart = writer->getFileOffset();
    Serializer serializer(writer);
    for (auto idx : order) {
        auto& rec = buffer[idx];
        serializer.write(rec.key.data(), numKeyBytes);
        for (auto& value : rec.payload) {
            value->serialize(serializer);
        }
    }
    writer->flush();
    runs.push_back(Run{runStart, static_cast<uint64_t>(buffer.size())});
    buffer.clear();
    bufferBytes = 0;
}

void ExternalMergeSort::finalize() {
    sealRun();
    if (writer != nullptr) {
        writer->flush();
    }
}

void ExternalMergeSort::collectRunSources(std::vector<RunSource>& out) {
    // Make this generator's runs durable before another instance reads them during the merge.
    if (writer != nullptr) {
        writer->flush();
    }
    out.reserve(out.size() + runs.size());
    for (auto& run : runs) {
        out.push_back(RunSource{fileInfo.get(), run.fileOffset, run.numTuples});
    }
}

void ExternalMergeSort::setMergeSources(std::vector<RunSource> sources) {
    mergeSources = std::move(sources);
    mergeTotalTuples = 0;
    for (auto& src : mergeSources) {
        mergeTotalTuples += src.numTuples;
    }
    isCoordinator = true;
}

void ExternalMergeSort::advanceCursor(MergeCursor& cursor) {
    if (cursor.remaining == 0) {
        cursor.valid = false;
        return;
    }
    cursor.headKey.resize(numKeyBytes);
    cursor.deser->read(cursor.headKey.data(), numKeyBytes);
    cursor.headPayload.clear();
    cursor.headPayload.reserve(numPayloadCols);
    for (auto c = 0u; c < numPayloadCols; c++) {
        std::shared_ptr<Value> value = Value::deserialize(*cursor.deser);
        cursor.headPayload.push_back(std::move(value));
    }
    cursor.remaining--;
    cursor.valid = true;
}

bool ExternalMergeSort::keyGreater(uint32_t a, uint32_t b) const {
    return compareRecords(cursors[a]->headKey.data(), cursors[a]->headPayload,
               cursors[b]->headKey.data(), cursors[b]->headPayload) > 0;
}

void ExternalMergeSort::initMerge() {
    if (mergeInitialized) {
        return;
    }
    mergeInitialized = true;
    if (writer != nullptr) {
        writer->flush();
    }
    // Standalone (library/self) use: no coordinator sources were set, so merge our own runs.
    if (mergeSources.empty() && !isCoordinator) {
        collectRunSources(mergeSources);
        mergeTotalTuples = numTuples;
    }
    cursors.reserve(mergeSources.size());
    for (auto& src : mergeSources) {
        auto cursor = std::make_unique<MergeCursor>();
        // Each run may live in a different generator's spill file (multi-threaded merge).
        auto reader = std::make_unique<BufferedFileReader>(*src.file);
        reader->resetReadOffset(src.fileOffset);
        cursor->deser = std::make_unique<Deserializer>(std::move(reader));
        cursor->remaining = src.numTuples;
        advanceCursor(*cursor);
        cursors.push_back(std::move(cursor));
    }
    for (auto i = 0u; i < cursors.size(); i++) {
        if (cursors[i]->valid) {
            heap.push_back(i);
        }
    }
    // A min-key heap on top of the STL max-heap: the "greatest" element under keyGreater is the one
    // with the smallest key, so it sits at the top and is popped first.
    auto cmp = [this](uint32_t a, uint32_t b) { return keyGreater(a, b); };
    std::make_heap(heap.begin(), heap.end(), cmp);
}

uint64_t ExternalMergeSort::scanNext(const std::vector<ValueVector*>& payloadVectors) {
    initMerge();
    if (!outputModeComputed) {
        outputModeComputed = true;
        // Batch mode (emit up to a full vector per call) only when every output column shares one
        // unflat state -- the common single factorization group case. Otherwise emit one tuple at a
        // time: flat outputs, possibly spanning multiple flat groups, must stay flat (one tuple per
        // vector), mirroring PayloadScanner's single-tuple path.
        outputBatchMode = !payloadVectors.empty() && !payloadVectors[0]->state->isFlat();
        if (outputBatchMode) {
            auto* sharedState = payloadVectors[0]->state.get();
            for (auto* vector : payloadVectors) {
                if (vector->state.get() != sharedState) {
                    outputBatchMode = false;
                    break;
                }
            }
        }
    }
    auto cmp = [this](uint32_t a, uint32_t b) { return keyGreater(a, b); };
    // In tuple-at-a-time mode `count` never exceeds 1, so writing each value at index `count` lands at
    // position 0 of every (flat) output vector -- consistent with the setToUnfiltered(count) below.
    const uint64_t cap = outputBatchMode ? DEFAULT_VECTOR_CAPACITY : 1;
    uint64_t count = 0;
    while (count < cap && !heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), cmp);
        const auto cursorIdx = heap.back();
        heap.pop_back();
        auto& cursor = *cursors[cursorIdx];
        for (auto c = 0u; c < numPayloadCols; c++) {
            payloadVectors[c]->copyFromValue(count, *cursor.headPayload[c]);
        }
        count++;
        advanceCursor(cursor);
        if (cursor.valid) {
            heap.push_back(cursorIdx);
            std::push_heap(heap.begin(), heap.end(), cmp);
        }
    }
    // Set every output state's selection to the emitted count. Batch mode has one shared unflat state;
    // tuple mode has one flat state per group (setting a shared state twice is idempotent).
    for (auto* vector : payloadVectors) {
        vector->state->getSelVectorUnsafe().setToUnfiltered(count);
    }
    return count;
}

} // namespace processor
} // namespace kuzu
