#include <cstdint>
#include <filesystem>

#include "common/constants.h"
#include "common/data_chunk/data_chunk_state.h"
#include "common/serializer/buffer_reader.h"
#include "common/serializer/buffer_writer.h"
#include "common/serializer/deserializer.h"
#include "common/serializer/serializer.h"
#include "common/system_config.h"
#include "common/types/types.h"
#include "common/types/value/value.h"
#include "common/vector/value_vector.h"
#include "graph_test/private_graph_test.h"
#include "gtest/gtest.h"
#include "processor/result/factorized_table.h"
#include "processor/result/factorized_table_util.h"
#include "processor/result/partitioned_factorized_table.h"
#include "spdlog/spdlog.h"
#include "storage/buffer_manager/buffer_manager.h"
#include "storage/buffer_manager/memory_manager.h"
#include "storage/buffer_manager/spiller.h"
#include "storage/enums/residency_state.h"
#include "storage/storage_manager.h"
#include "storage/table/chunked_node_group.h"
#include "storage/table/column_chunk.h"

using namespace kuzu::common;
using namespace kuzu::storage;

namespace kuzu {
namespace testing {

class BufferManagerTest : public DBTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKuzuRootPath("dataset/tinysnb/");
    }
    void reserveAll() {
        auto* bm = getBufferManager(*database);
        // Can't use UINT64_MAX since it will overflow the usedMemory
        ASSERT_FALSE(bm->reserve(UINT64_MAX / 2));
    }
};

TEST_F(BufferManagerTest, TestBMUsageForIdenticalQueries) {
    auto bm = getBufferManager(*database);
    auto initialMemoryUsage = bm->getUsedMemory();
    auto numRuns = 10u;
    auto query = "MATCH (p:person) WHERE p.ID = 0 RETURN p.fName";
    for (auto i = 0u; i < numRuns; ++i) {
        auto result = conn->query(query);
        ASSERT_TRUE(result->isSuccess()) << result->toString();
    }
    auto memoryUsed = bm->getUsedMemory();
    for (auto i = 0u; i < numRuns; ++i) {
        auto result = conn->query(query);
        ASSERT_TRUE(result->isSuccess()) << result->toString();
    }
    ASSERT_EQ(memoryUsed, bm->getUsedMemory())
        << "Memory usage after two identical queries should be identical";
    spdlog::info("Memory used initially: {}", initialMemoryUsage);
    spdlog::info("Memory used after transactions: {}", memoryUsed);
}

// Verifies that a factorized table's rows survive a position-independent serialize/deserialize
// round-trip (including variable-length string data), which is the basis for spilling to disk.
TEST_F(BufferManagerTest, FactorizedTableSerializeRoundTrip) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());

    FactorizedTable table(mm,
        FactorizedTableUtils::createFlatTableSchema(LogicalType::copy(columnTypes)));
    auto state = DataChunkState::getSingleValueDataChunkState();
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector strVector(LogicalType::STRING(), mm);
    intVector.state = state;
    strVector.state = state;
    const uint64_t numRows = 5;
    for (auto i = 0u; i < numRows; i++) {
        intVector.setNull(0, false);
        intVector.setValue<int64_t>(0, static_cast<int64_t>(i) * 100);
        strVector.setNull(0, false);
        StringVector::addString(&strVector, 0, "row-" + std::to_string(i));
        std::vector<ValueVector*> vectors{&intVector, &strVector};
        table.append(vectors);
    }
    ASSERT_EQ(table.getNumTuples(), numRows);

    auto writer = std::make_shared<BufferWriter>();
    Serializer serializer(writer);
    table.serialize(serializer, columnTypes);
    auto blob = writer->getData();
    Deserializer deserializer(std::make_unique<BufferReader>(blob.data.get(), blob.size));
    auto restored = FactorizedTable::deserialize(deserializer, mm, columnTypes);
    ASSERT_EQ(restored->getNumTuples(), numRows);

    std::vector<std::unique_ptr<Value>> valueHolders;
    std::vector<Value*> values;
    for (auto& type : columnTypes) {
        valueHolders.push_back(std::make_unique<Value>(Value::createDefaultValue(type.copy())));
        values.push_back(valueHolders.back().get());
    }
    FlatTupleIterator iterator(*restored, values);
    uint64_t row = 0;
    while (iterator.hasNextFlatTuple()) {
        iterator.getNextFlatTuple();
        ASSERT_EQ(values[0]->getValue<int64_t>(), static_cast<int64_t>(row) * 100);
        ASSERT_EQ(values[1]->getValue<std::string>(), "row-" + std::to_string(row));
        row++;
    }
    ASSERT_EQ(row, numRows);
}

// Verifies the core of out-of-core (Grace) hash join / partitioned aggregation: build-side tuples
// are radix-scattered into partitions by the high bits of their hash, each partition can be spilled
// to disk (position-independent) and reloaded, and appending to a spilled partition transparently
// reloads it. Covers variable-length (string) data, which is why the serialized spill path is used.
TEST_F(BufferManagerTest, PartitionedFactorizedTableSpillReload) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());

    // logNumPartitions = 1 -> 2 partitions; the top hash bit selects the partition.
    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_test.spill").string();
    PartitionedFactorizedTable partitioned(mm, LogicalType::copy(columnTypes), 1, fs, spillPath);
    ASSERT_EQ(partitioned.getNumPartitions(), 2u);

    const uint64_t numRows = 200;
    auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    state->initOriginalAndSelectedSize(numRows);
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector strVector(LogicalType::STRING(), mm);
    ValueVector hashVector(LogicalType::INT64(), mm);
    intVector.state = state;
    strVector.state = state;
    hashVector.state = state;
    for (auto i = 0u; i < numRows; i++) {
        intVector.setNull(i, false);
        intVector.setValue<int64_t>(i, static_cast<int64_t>(i));
        strVector.setNull(i, false);
        StringVector::addString(&strVector, i, "row-" + std::to_string(i));
        hashVector.setNull(i, false);
        // Even rows -> top bit 0 -> partition 0; odd rows -> top bit 1 -> partition 1.
        hashVector.setValue<uint64_t>(i, (i % 2 == 0) ? 0ULL : (1ULL << 63));
    }
    std::vector<ValueVector*> vectors{&intVector, &strVector};
    partitioned.appendVectors(vectors, hashVector);

    ASSERT_EQ(partitioned.getNumTuples(), numRows);
    ASSERT_EQ(partitioned.getPartitionNumTuples(0), numRows / 2);
    ASSERT_EQ(partitioned.getPartitionNumTuples(1), numRows / 2);

    // Spill everything; tuple counts must survive the spill.
    ASSERT_EQ(partitioned.spillAllPartitions(), 2u);
    ASSERT_TRUE(partitioned.isSpilled(0));
    ASSERT_TRUE(partitioned.isSpilled(1));
    ASSERT_EQ(partitioned.getNumTuples(), numRows);

    // Reload each partition and verify its rows survived the round trip in append order.
    auto verifyPartition = [&](common::idx_t p, uint64_t firstValue) {
        auto& table = partitioned.getResidentPartition(p);
        ASSERT_FALSE(partitioned.isSpilled(p));
        ASSERT_EQ(table.getNumTuples(), numRows / 2);
        std::vector<std::unique_ptr<Value>> valueHolders;
        std::vector<Value*> values;
        for (auto& type : columnTypes) {
            valueHolders.push_back(std::make_unique<Value>(Value::createDefaultValue(type.copy())));
            values.push_back(valueHolders.back().get());
        }
        FlatTupleIterator it(table, values);
        uint64_t expected = firstValue;
        uint64_t count = 0;
        while (it.hasNextFlatTuple()) {
            it.getNextFlatTuple();
            ASSERT_EQ(values[0]->getValue<int64_t>(), static_cast<int64_t>(expected));
            ASSERT_EQ(values[1]->getValue<std::string>(), "row-" + std::to_string(expected));
            expected += 2;
            count++;
        }
        ASSERT_EQ(count, numRows / 2);
    };
    verifyPartition(0, 0); // even rows
    verifyPartition(1, 1); // odd rows

    // Spill again, then append a second identical batch: appending to a spilled partition must
    // transparently reload it first.
    ASSERT_EQ(partitioned.spillAllPartitions(), 2u);
    ASSERT_TRUE(partitioned.isSpilled(0));
    partitioned.appendVectors(vectors, hashVector);
    ASSERT_FALSE(partitioned.isSpilled(0));
    ASSERT_FALSE(partitioned.isSpilled(1));
    ASSERT_EQ(partitioned.getPartitionNumTuples(0), numRows);
    ASSERT_EQ(partitioned.getPartitionNumTuples(1), numRows);
    ASSERT_EQ(partitioned.getNumTuples(), numRows * 2);
}

// Verifies the operator-triggered spill driver: spilling the largest resident partitions until the
// resident tuple bytes fit a budget, without losing tuples, and reloadable afterward.
TEST_F(BufferManagerTest, PartitionedFactorizedTableBudgetSpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());

    const common::idx_t logNumPartitions = 3; // 8 partitions
    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_budget.spill").string();
    PartitionedFactorizedTable partitioned(mm, LogicalType::copy(columnTypes), logNumPartitions, fs,
        spillPath);
    ASSERT_EQ(partitioned.getNumPartitions(), 8u);

    const uint64_t numRows = 800;
    auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    state->initOriginalAndSelectedSize(numRows);
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector strVector(LogicalType::STRING(), mm);
    ValueVector hashVector(LogicalType::INT64(), mm);
    intVector.state = state;
    strVector.state = state;
    hashVector.state = state;
    for (auto i = 0u; i < numRows; i++) {
        intVector.setNull(i, false);
        intVector.setValue<int64_t>(i, static_cast<int64_t>(i));
        strVector.setNull(i, false);
        StringVector::addString(&strVector, i, "v" + std::to_string(i));
        hashVector.setNull(i, false);
        // Spread rows evenly across the 8 partitions using the top 3 hash bits.
        hashVector.setValue<uint64_t>(i, static_cast<uint64_t>(i % 8) << 61);
    }
    std::vector<ValueVector*> vectors{&intVector, &strVector};
    partitioned.appendVectors(vectors, hashVector);
    ASSERT_EQ(partitioned.getNumTuples(), numRows);

    const auto residentBefore = partitioned.getResidentTupleBytes();
    ASSERT_GT(residentBefore, 0u);

    // Drive spilling down to half the current resident bytes.
    const auto budget = residentBefore / 2;
    const auto numSpilled = partitioned.spillToReduceResidentBytesTo(budget);
    ASSERT_GT(numSpilled, 0u);
    ASSERT_LE(partitioned.getResidentTupleBytes(), budget);
    // Tuple counts must be preserved across the spill.
    ASSERT_EQ(partitioned.getNumTuples(), numRows);

    // Every spilled partition must reload to its original tuple count.
    uint64_t reloadedTotal = 0;
    for (common::idx_t p = 0; p < partitioned.getNumPartitions(); p++) {
        auto expected = partitioned.getPartitionNumTuples(p);
        auto& table = partitioned.getResidentPartition(p);
        ASSERT_EQ(table.getNumTuples(), expected);
        reloadedTotal += table.getNumTuples();
    }
    ASSERT_EQ(reloadedTotal, numRows);
}

// Stresses the spill file I/O at scale: partitions whose serialized form spans many pages, appended
// across many batches, exercise BufferedFileWriter multi-page flush, large-blob reload, and
// multi-block FactorizedTable deserialize. Also covers the single-partition (logNumPartitions = 0)
// edge case where every row routes to partition 0.
TEST_F(BufferManagerTest, PartitionedFactorizedTableLargeMultiPageSpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());

    const common::idx_t logNumPartitions = 2; // 4 partitions
    const uint64_t numPartitions = 4;
    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_large.spill").string();
    PartitionedFactorizedTable partitioned(mm, LogicalType::copy(columnTypes), logNumPartitions, fs,
        spillPath);

    // ~40k rows with sizeable strings -> each of the 4 partitions serializes to well over one 256KB
    // page, so spill/reload crosses many pages.
    const uint64_t numRows = 40000;
    const uint64_t batchSize = DEFAULT_VECTOR_CAPACITY;
    auto makeString = [](uint64_t i) {
        return "value-" + std::to_string(i) + "-0123456789abcdefghij";
    };
    auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector strVector(LogicalType::STRING(), mm);
    ValueVector hashVector(LogicalType::INT64(), mm);
    intVector.state = state;
    strVector.state = state;
    hashVector.state = state;
    std::vector<ValueVector*> vectors{&intVector, &strVector};
    for (uint64_t base = 0; base < numRows; base += batchSize) {
        const auto rowsInBatch = std::min(batchSize, numRows - base);
        state->initOriginalAndSelectedSize(rowsInBatch);
        for (auto j = 0u; j < rowsInBatch; j++) {
            const auto i = base + j;
            intVector.setNull(j, false);
            intVector.setValue<int64_t>(j, static_cast<int64_t>(i));
            strVector.setNull(j, false);
            StringVector::addString(&strVector, j, makeString(i));
            hashVector.setNull(j, false);
            // Route by i % 4 using the top 2 hash bits.
            hashVector.setValue<uint64_t>(j, static_cast<uint64_t>(i % numPartitions) << 62);
        }
        partitioned.appendVectors(vectors, hashVector);
    }
    ASSERT_EQ(partitioned.getNumTuples(), numRows);
    ASSERT_EQ(partitioned.spillAllPartitions(), numPartitions);
    ASSERT_EQ(partitioned.getNumTuples(), numRows);

    // Reload each partition and verify every row (partition p holds i where i % 4 == p, in order).
    uint64_t verified = 0;
    for (common::idx_t p = 0; p < numPartitions; p++) {
        auto& table = partitioned.getResidentPartition(p);
        std::vector<std::unique_ptr<Value>> valueHolders;
        std::vector<Value*> values;
        for (auto& type : columnTypes) {
            valueHolders.push_back(std::make_unique<Value>(Value::createDefaultValue(type.copy())));
            values.push_back(valueHolders.back().get());
        }
        FlatTupleIterator it(table, values);
        uint64_t expected = p;
        while (it.hasNextFlatTuple()) {
            it.getNextFlatTuple();
            ASSERT_EQ(values[0]->getValue<int64_t>(), static_cast<int64_t>(expected));
            ASSERT_EQ(values[1]->getValue<std::string>(), makeString(expected));
            expected += numPartitions;
            verified++;
        }
    }
    ASSERT_EQ(verified, numRows);
}

// Single-partition edge case: logNumPartitions = 0 -> everything routes to partition 0.
TEST_F(BufferManagerTest, PartitionedFactorizedTableSinglePartition) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_single.spill").string();
    PartitionedFactorizedTable partitioned(mm, LogicalType::copy(columnTypes), 0, fs, spillPath);
    ASSERT_EQ(partitioned.getNumPartitions(), 1u);
    // Any hash maps to partition 0.
    ASSERT_EQ(partitioned.getPartitionIdxForHash(0), 0u);
    ASSERT_EQ(partitioned.getPartitionIdxForHash(~0ULL), 0u);

    const uint64_t numRows = 100;
    auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    state->initOriginalAndSelectedSize(numRows);
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector hashVector(LogicalType::INT64(), mm);
    intVector.state = state;
    hashVector.state = state;
    for (auto i = 0u; i < numRows; i++) {
        intVector.setNull(i, false);
        intVector.setValue<int64_t>(i, static_cast<int64_t>(i));
        hashVector.setNull(i, false);
        hashVector.setValue<uint64_t>(i, static_cast<uint64_t>(i)); // arbitrary hashes
    }
    std::vector<ValueVector*> vectors{&intVector};
    partitioned.appendVectors(vectors, hashVector);
    ASSERT_EQ(partitioned.getPartitionNumTuples(0), numRows);
    ASSERT_EQ(partitioned.spillAllPartitions(), 1u);
    auto& table = partitioned.getResidentPartition(0);
    ASSERT_EQ(table.getNumTuples(), numRows);
}

class EmptyBufferManagerTest : public DBTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKuzuRootPath("dataset/empty-db/");
    }
};

TEST_F(EmptyBufferManagerTest, TestSpillToDiskMemoryUsage) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    auto bm = getBufferManager(*database);
    auto mm = getMemoryManager(*database);
    auto initialUsedMemory = bm->getUsedMemory();
    {
        auto buffer = mm->allocateBuffer(false, 1024);
        ASSERT_EQ(initialUsedMemory + 1024, bm->getUsedMemory());
        ASSERT_NE(buffer->getBuffer().data(), nullptr);
    }
    ASSERT_EQ(initialUsedMemory, bm->getUsedMemory());

    {
        std::vector<std::unique_ptr<ColumnChunk>> chunks;
        chunks.push_back(std::make_unique<ColumnChunk>(*mm, false,
            std::make_unique<ColumnChunkData>(*mm, LogicalType(LogicalTypeID::INT64), 1024, false,
                ResidencyState::IN_MEMORY, false)));
        auto chunkedNodeGroup = ChunkedNodeGroup(std::move(chunks), 0);
        chunkedNodeGroup.setUnused(*mm);
        auto memoryWithChunks = bm->getUsedMemory();
        SpillResult memorySpilled{};
        bm->getSpillerOrSkip([&](auto& spiller) {
            spiller.addUnusedComponent(&chunkedNodeGroup);
            // Claim memory from unused chunks
            memorySpilled = spiller.claimNextComponent();
        });
        ASSERT_NE(memorySpilled.memoryFreed + memorySpilled.memoryNowEvictable, 0);
        // The chunks should be entirely on disk
        ASSERT_EQ(initialUsedMemory, bm->getUsedMemory() - memorySpilled.memoryFreed);
        chunkedNodeGroup.loadFromDisk(*mm);
        // The chunks should be back in memory, but Spiller::claimNextComponent does not update
        // the amount of used memory itself, so we end up with the spilled memory recorded twice
        ASSERT_EQ(memoryWithChunks, bm->getUsedMemory() - memorySpilled.memoryFreed);
    }

    {
        // The memory manager uses the buffer manager for allocations of size TEMP_PAGE_SIZE
        std::vector<std::unique_ptr<ColumnChunk>> chunks;
        chunks.push_back(std::make_unique<ColumnChunk>(*mm, false,
            std::make_unique<ColumnChunkData>(*mm, LogicalType(LogicalTypeID::INT64),
                TEMP_PAGE_SIZE / sizeof(int64_t), false, ResidencyState::IN_MEMORY, false)));
        auto chunkedNodeGroup = ChunkedNodeGroup(std::move(chunks), 0);
        chunkedNodeGroup.setUnused(*mm);
        SpillResult memorySpilled{};
        bm->getSpillerOrSkip([&](auto& spiller) {
            spiller.addUnusedComponent(&chunkedNodeGroup);
            // Claim memory from unused chunks
            memorySpilled = spiller.claimNextComponent();
        });
        ASSERT_EQ(memorySpilled.memoryFreed, 0);
        ASSERT_EQ(memorySpilled.memoryNowEvictable, TEMP_PAGE_SIZE);
    }
}

// Simulates the case where we try to evict a page during an optimistic read
TEST_F(BufferManagerTest, TestBMEvictionSlowRead) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    auto* fh = getClientContext(*conn)->getStorageManager()->getDataFH();

    auto* page = fh->pinPage(0, PageReadPolicy::READ_PAGE);
    *page = 112;
    fh->getPageState(0)->setDirty();
    fh->unpinPage(0);
    bool firstTry = true;
    // The mmap version will fail the first time, but will be run again since the page state was
    // changed during the read
#if BM_MALLOC
    fh->optimisticReadPage(0, [&](auto* frame) {
        ASSERT_TRUE(firstTry);
        firstTry = false;
        ASSERT_EQ(*frame, 112);
        // Should evict all evictable candidates in the eviction queue before failing
        // Should *not* evict the page we're currently reading
        reserveAll();
        // Probably will still succeed, but with ASAN (and BM_MALLOC) it should catch a read after
        // free if this frame was evicted
        ASSERT_EQ(*frame, 112);
    });
#else
    fh->optimisticReadPage(0, [&](auto* frame) {
        if (firstTry) {
            firstTry = false;
            ASSERT_EQ(*frame, 112);
            // Should evict all evictable candidates in the eviction queue before failing
            // Should evict the page we're currently reading
            reserveAll();
            // Frame was evicted and should now be zeroed
            // (not on macos, which does this lazily)
#ifndef __APPLE__
            ASSERT_EQ(*frame, 0);
#endif
        } else {
            // Second try it should be correct again
            ASSERT_EQ(*frame, 112);
        }
    });
#endif
}

} // namespace testing
} // namespace kuzu
