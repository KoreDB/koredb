#include "processor/result/partitioned_factorized_table.h"

#include <cstring>
#include <filesystem>

#include "common/assert.h"
#include "common/data_chunk/data_chunk_state.h"
#include "common/file_system/file_info.h"
#include "common/file_system/virtual_file_system.h"
#include "common/serializer/buffer_reader.h"
#include "common/serializer/buffered_file.h"
#include "common/serializer/deserializer.h"
#include "common/serializer/serializer.h"
#include "common/vector/value_vector.h"
#include "processor/result/factorized_table_util.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

PartitionedFactorizedTable::PartitionedFactorizedTable(MemoryManager* mm,
    std::vector<LogicalType> columnTypes, idx_t logNumPartitions, VirtualFileSystem* vfs,
    std::string tmpFilePath)
    : mm{mm}, columnTypes{std::move(columnTypes)}, logNumPartitions{logNumPartitions}, vfs{vfs},
      tmpFilePath{std::move(tmpFilePath)} {
    const auto numPartitions = static_cast<idx_t>(1) << logNumPartitions;
    partitions.reserve(numPartitions);
    for (auto p = 0u; p < numPartitions; p++) {
        partitions.push_back(std::make_unique<FactorizedTable>(mm, createSchema()));
    }
    spillStates.resize(numPartitions);
    scatterBuckets.resize(numPartitions);
}

PartitionedFactorizedTable::~PartitionedFactorizedTable() {
    // Close the file handle before removing the backing temp file. The spill file is a plain local
    // temp file (spilling only supports a LocalFileSystem), so it is removed directly rather than
    // through VirtualFileSystem::removeFileIfExists, which restricts removal to the database's own
    // file set. Cleanup is best-effort and must never throw from a destructor.
    fileInfo.reset();
    if (!tmpFilePath.empty()) {
        std::error_code errCode;
        std::filesystem::remove(tmpFilePath, errCode);
    }
}

FactorizedTableSchema PartitionedFactorizedTable::createSchema() const {
    return FactorizedTableUtils::createFlatTableSchema(LogicalType::copy(columnTypes));
}

FileInfo* PartitionedFactorizedTable::getOrCreateFile() {
    if (fileInfo == nullptr) {
        fileInfo =
            vfs->openFile(tmpFilePath, FileOpenFlags(FileFlags::READ_ONLY | FileFlags::WRITE |
                                                     FileFlags::CREATE_AND_TRUNCATE_IF_EXISTS));
    }
    return fileInfo.get();
}

void PartitionedFactorizedTable::appendVectors(const std::vector<ValueVector*>& vectors,
    const ValueVector& hashVector) {
    KU_ASSERT(!vectors.empty());
    auto* state = vectors[0]->state.get();
    // Batch scatter expects an unflat (multi-row) chunk: append() flattens the selected rows of an
    // unflat input into the flat partition schema, one output tuple per selected position.
    KU_ASSERT(!state->isFlat());
    auto& selVector = state->getSelVectorUnsafe();
    const auto numTuples = selVector.getSelSize();
    if (numTuples == 0) {
        return;
    }
    for (auto& bucket : scatterBuckets) {
        bucket.clear();
    }
    for (auto i = 0u; i < numTuples; i++) {
        const auto pos = selVector[i];
        const auto h = hashVector.getValue<hash_t>(pos);
        scatterBuckets[getPartitionIdxForHash(h)].push_back(pos);
    }
    // Snapshot the original selection so it can be restored after temporarily installing a
    // per-bucket selection on the shared state.
    const bool wasUnfiltered = selVector.isUnfiltered();
    std::vector<sel_t> saved(selVector.getSelectedPositions().begin(),
        selVector.getSelectedPositions().end());
    for (auto p = 0u; p < scatterBuckets.size(); p++) {
        auto& bucket = scatterBuckets[p];
        if (bucket.empty()) {
            continue;
        }
        // Any partition receiving tuples must be resident before we append to it.
        reloadPartition(p);
        selVector.setToFiltered();
        std::memcpy(selVector.getMutableBuffer().data(), bucket.data(),
            bucket.size() * sizeof(sel_t));
        selVector.setSelSize(static_cast<sel_t>(bucket.size()));
        partitions[p]->append(vectors);
    }
    // Restore the original selection.
    if (wasUnfiltered) {
        selVector.setToUnfiltered(static_cast<sel_t>(saved.size()));
    } else {
        selVector.setToFiltered(static_cast<sel_t>(saved.size()));
        std::memcpy(selVector.getMutableBuffer().data(), saved.data(),
            saved.size() * sizeof(sel_t));
    }
}

uint64_t PartitionedFactorizedTable::getPartitionNumTuples(idx_t partitionIdx) const {
    return spillStates[partitionIdx].spilled ? spillStates[partitionIdx].numTuples :
                                               partitions[partitionIdx]->getNumTuples();
}

uint64_t PartitionedFactorizedTable::getNumTuples() const {
    uint64_t total = 0;
    for (auto p = 0u; p < partitions.size(); p++) {
        total += getPartitionNumTuples(p);
    }
    return total;
}

FactorizedTable& PartitionedFactorizedTable::getResidentPartition(idx_t partitionIdx) {
    reloadPartition(partitionIdx);
    return *partitions[partitionIdx];
}

void PartitionedFactorizedTable::spillPartition(idx_t partitionIdx) {
    auto& spillState = spillStates[partitionIdx];
    if (spillState.spilled) {
        return;
    }
    auto& table = *partitions[partitionIdx];
    const auto numTuples = table.getNumTuples();
    if (numTuples == 0) {
        return;
    }
    auto* file = getOrCreateFile();
    {
        auto writer = std::make_shared<BufferedFileWriter>(*file);
        writer->setFileOffset(fileWriteOffset);
        Serializer serializer(writer);
        table.serialize(serializer, columnTypes);
        writer->flush();
        const auto endOffset = writer->getFileOffset();
        spillState.spilled = true;
        spillState.fileOffset = fileWriteOffset;
        spillState.byteSize = endOffset - fileWriteOffset;
        spillState.numTuples = numTuples;
        fileWriteOffset = endOffset;
    }
    // Free the in-memory blocks by replacing the partition with a fresh empty table. The freed
    // DataBlocks return to the MemoryManager's free-page pool for reuse.
    partitions[partitionIdx] = std::make_unique<FactorizedTable>(mm, createSchema());
}

void PartitionedFactorizedTable::reloadPartition(idx_t partitionIdx) {
    auto& spillState = spillStates[partitionIdx];
    if (!spillState.spilled) {
        return;
    }
    KU_ASSERT(fileInfo != nullptr);
    auto blob = std::make_unique<uint8_t[]>(spillState.byteSize);
    fileInfo->readFromFile(blob.get(), spillState.byteSize, spillState.fileOffset);
    Deserializer deserializer(std::make_unique<BufferReader>(blob.get(), spillState.byteSize));
    partitions[partitionIdx] = FactorizedTable::deserialize(deserializer, mm, columnTypes);
    spillState.spilled = false;
    spillState.byteSize = 0;
    spillState.numTuples = 0;
}

idx_t PartitionedFactorizedTable::spillAllPartitions() {
    idx_t numSpilled = 0;
    for (auto p = 0u; p < partitions.size(); p++) {
        if (!spillStates[p].spilled && partitions[p]->getNumTuples() > 0) {
            spillPartition(p);
            numSpilled++;
        }
    }
    return numSpilled;
}

uint64_t PartitionedFactorizedTable::spillLargestResidentPartition() {
    idx_t largest = 0;
    uint64_t largestNumTuples = 0;
    for (auto p = 0u; p < partitions.size(); p++) {
        if (spillStates[p].spilled) {
            continue;
        }
        const auto numTuples = partitions[p]->getNumTuples();
        if (numTuples > largestNumTuples) {
            largestNumTuples = numTuples;
            largest = p;
        }
    }
    if (largestNumTuples == 0) {
        return 0;
    }
    spillPartition(largest);
    return largestNumTuples;
}

} // namespace processor
} // namespace kuzu
