#pragma once

#include <queue>

#include "processor/operator/order_by/radix_sort.h"
#include "processor/result/factorized_table.h"

namespace koredb {
namespace processor {

class ExternalMergeSort;

class SortSharedState {
public:
    // Constructor and destructor are out-of-line because externalSorter is a unique_ptr to a type
    // that is incomplete in this header (defining them inline would force every user of this class to
    // see the complete ExternalMergeSort for member cleanup).
    SortSharedState();
    ~SortSharedState();

    inline uint64_t getNumBytesPerTuple() const { return numBytesPerTuple; }

    // Out-of-core (external merge) sort path, chosen at runtime when spill_order_by is on and the
    // sort is eligible; when inactive the in-memory payloadTables/sortedKeyBlocks path runs.
    //
    // Multi-threaded run generation: each OrderBy thread registers its own generator here (thread-safe,
    // mirroring getLocalPayloadTable). After the barrier, prepareExternalMerge (called once on the
    // single scan thread) hands every generator's runs to one coordinator for a single k-way merge.
    ExternalMergeSort* addExternalGenerator(std::unique_ptr<ExternalMergeSort> generator);
    ExternalMergeSort* prepareExternalMerge();
    void setExternalActive() { externalActive = true; }
    bool isExternalActive() const { return externalActive; }

    inline std::vector<StrKeyColInfo>& getStrKeyColInfo() { return strKeyColsInfo; }

    inline std::queue<std::shared_ptr<MergedKeyBlocks>>* getSortedKeyBlocks() {
        return sortedKeyBlocks.get();
    }

    void init(const OrderByDataInfo& orderByDataInfo);

    std::pair<uint64_t, FactorizedTable*> getLocalPayloadTable(
        storage::MemoryManager& memoryManager, const FactorizedTableSchema& payloadTableSchema);

    void appendLocalSortedKeyBlock(const std::shared_ptr<MergedKeyBlocks>& mergedDataBlocks);

    void combineFTHasNoNullGuarantee();

    std::vector<FactorizedTable*> getPayloadTables() const;

    inline MergedKeyBlocks* getMergedKeyBlock() const {
        return sortedKeyBlocks->empty() ? nullptr : sortedKeyBlocks->front().get();
    }

private:
    std::mutex mtx;
    std::vector<std::unique_ptr<FactorizedTable>> payloadTables;
    uint8_t nextTableIdx;
    std::unique_ptr<std::queue<std::shared_ptr<MergedKeyBlocks>>> sortedKeyBlocks;
    uint32_t numBytesPerTuple;
    std::vector<StrKeyColInfo> strKeyColsInfo;
    // Per-thread external-sort run generators; index 0 doubles as the merge coordinator.
    std::vector<std::unique_ptr<ExternalMergeSort>> externalGenerators;
    ExternalMergeSort* externalCoordinator = nullptr;
    bool externalMergePrepared = false;
    bool externalActive = false;
};

class SortLocalState {
public:
    void init(const OrderByDataInfo& orderByDataInfo, SortSharedState& sharedState,
        storage::MemoryManager* memoryManager);

    void append(const std::vector<common::ValueVector*>& keyVectors,
        const std::vector<common::ValueVector*>& payloadVectors);

    void finalize(SortSharedState& sharedState);

private:
    std::unique_ptr<OrderByKeyEncoder> orderByKeyEncoder;
    std::unique_ptr<RadixSort> radixSorter;
    uint64_t globalIdx = UINT64_MAX;
    FactorizedTable* payloadTable = nullptr;
};

class PayloadScanner {
public:
    PayloadScanner(MergedKeyBlocks* keyBlockToScan, std::vector<FactorizedTable*> payloadTables,
        uint64_t skipNumber = UINT64_MAX, uint64_t limitNumber = UINT64_MAX);

    uint64_t scan(std::vector<common::ValueVector*> vectorsToRead);

private:
    bool scanSingleTuple(std::vector<common::ValueVector*> vectorsToRead) const;

    void applyLimitOnResultVectors(std::vector<common::ValueVector*> vectorsToRead);

private:
    bool hasUnflatColInPayload;
    uint32_t payloadIdxOffset;
    std::vector<uint32_t> colsToScan;
    std::unique_ptr<uint8_t*[]> tuplesToRead;
    std::unique_ptr<BlockPtrInfo> blockPtrInfo;
    MergedKeyBlocks* keyBlockToScan;
    uint32_t nextTupleIdxToReadInMergedKeyBlock;
    uint64_t endTuplesIdxToReadInMergedKeyBlock;
    std::vector<FactorizedTable*> payloadTables;
    uint64_t limitNumber;
};

} // namespace processor
} // namespace koredb
