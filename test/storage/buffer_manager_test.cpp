#include <cstdint>

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
