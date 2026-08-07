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
    // Column-by-column, resolving each STRING column's prefix tie against the full payload string
    // before moving on. This mirrors KeyBlockMerger::compareTuplePtrWithStringCol exactly (including
    // that trailing non-string columns after the last string column are treated as a tie), so the
    // spilled order is identical to the in-memory sort.
    const uint32_t strEncSize =
        OrderByKeyEncoder::getEncodingSize(LogicalType(LogicalTypeID::STRING));
    uint32_t lastComparedBytes = 0;
    for (auto& sc : strKeyCols) {
        const auto cmpLen = sc.offsetInEncodedKey - lastComparedBytes + strEncSize;
        int result = std::memcmp(keyA + lastComparedBytes, keyB + lastComparedBytes, cmpLen);
        const auto* leftStrPtr = keyA + sc.offsetInEncodedKey;
        const auto* rightStrPtr = keyB + sc.offsetInEncodedKey;
        if (OrderByKeyEncoder::isNullVal(leftStrPtr, sc.isAsc) &&
            OrderByKeyEncoder::isNullVal(rightStrPtr, sc.isAsc)) {
            lastComparedBytes = sc.offsetInEncodedKey + strEncSize;
            continue;
        }
        if (result == 0) {
            const bool leftLong = OrderByKeyEncoder::isLongStr(leftStrPtr, sc.isAsc);
            const bool rightLong = OrderByKeyEncoder::isLongStr(rightStrPtr, sc.isAsc);
            if (!leftLong && !rightLong) {
                continue; // both fit in the prefix -> equal on this column
            } else if (leftLong && !rightLong) {
                return sc.isAsc ? 1 : -1;
            } else if (!leftLong && rightLong) {
                return sc.isAsc ? -1 : 1;
            }
            const std::string sA = payloadA[sc.payloadColIdx]->getValue<std::string>();
            const std::string sB = payloadB[sc.payloadColIdx]->getValue<std::string>();
            if (sA == sB) {
                lastComparedBytes = sc.offsetInEncodedKey + strEncSize;
                continue;
            }
            return (sc.isAsc == (sA > sB)) ? 1 : -1;
        }
        return result;
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
        const auto pos = keyState.getSelVector()[t];
        InMemRecord rec;
        rec.key = std::move(batchKeys[t]);
        rec.payload.reserve(numPayloadCols);
        for (auto c = 0u; c < numPayloadCols; c++) {
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
    cursors.reserve(runs.size());
    for (auto& run : runs) {
        auto cursor = std::make_unique<MergeCursor>();
        auto reader = std::make_unique<BufferedFileReader>(*fileInfo);
        reader->resetReadOffset(run.fileOffset);
        cursor->deser = std::make_unique<Deserializer>(std::move(reader));
        cursor->remaining = run.numTuples;
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
    auto cmp = [this](uint32_t a, uint32_t b) { return keyGreater(a, b); };
    uint64_t count = 0;
    while (count < DEFAULT_VECTOR_CAPACITY && !heap.empty()) {
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
    if (!payloadVectors.empty()) {
        payloadVectors[0]->state->getSelVectorUnsafe().setToUnfiltered(count);
    }
    return count;
}

} // namespace processor
} // namespace kuzu
