#pragma once

#include "storage/buffer_manager/memory_manager.h"
#include "storage/buffer_manager/spillable.h"
#include "storage/file_handle.h"

namespace kuzu {
namespace common {
class VirtualFileSystem;
};
namespace storage {
class BufferManager;
class ColumnChunkData;

// This should only be used with a LocalFileSystem
class Spiller {
public:
    Spiller(std::string tmpFilePath, BufferManager& bufferManager, common::VirtualFileSystem* vfs);
    // Registers a component whose (inactive) data may be spilled to disk to reclaim memory.
    void addUnusedComponent(SpillableComponent* component);
    // Deregisters a component so that it will no longer be considered for spilling (e.g. because its
    // data is about to be accessed again).
    void clearUnusedComponent(SpillableComponent* component);
    SpillResult spillToDisk(ColumnChunkData& chunk) const;
    void loadFromDisk(ColumnChunkData& chunk) const;
    // Reclaims memory from the next registered spillable component in the set and returns the amount
    // of memory reclaimed. If the set is empty, returns zero.
    SpillResult claimNextComponent();
    // Must only be used once all chunks have been loaded from disk.
    void clearFile();
    ~Spiller();

private:
    FileHandle* getOrCreateDataFH() const;
    FileHandle* getDataFH() const;

private:
    std::string tmpFilePath;
    BufferManager& bufferManager;
    common::VirtualFileSystem* vfs;
    std::unordered_set<SpillableComponent*> spillableComponents;
    std::atomic<FileHandle*> dataFH;
    std::mutex spillableComponentsMtx;
    mutable std::mutex fileCreationMutex;
};

} // namespace storage
} // namespace kuzu
