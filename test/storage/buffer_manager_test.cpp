#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>

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
#include "function/aggregate/count_star.h"
#include "function/aggregate/sum.h"
#include "function/aggregate_function.h"
#include "function/hash/vector_hash_functions.h"
#include "graph_test/private_graph_test.h"
#include "gtest/gtest.h"
#include "processor/data_pos.h"
#include "processor/operator/aggregate/hash_aggregate.h"
#include "processor/operator/aggregate/partitioned_aggregate_executor.h"
#include "processor/operator/hash_join/grace_hash_join_executor.h"
#include "processor/operator/order_by/external_merge_sort.h"
#include "processor/result/factorized_table_util.h"
#include "processor/operator/hash_join/hash_join_build.h"
#include "processor/operator/hash_join/join_hash_table.h"
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

// Verifies partition-wise merge of two per-thread partitioned tables (the parallel-build combine
// step), including reload of a spilled source partition before merging.
TEST_F(BufferManagerTest, PartitionedFactorizedTableMerge) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());

    const auto pathA =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_mergeA.spill").string();
    const auto pathB =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_mergeB.spill").string();
    PartitionedFactorizedTable tableA(mm, LogicalType::copy(columnTypes), 1, fs, pathA);
    PartitionedFactorizedTable tableB(mm, LogicalType::copy(columnTypes), 1, fs, pathB);

    auto fill = [&](PartitionedFactorizedTable& t, uint64_t begin, uint64_t end) {
        const auto n = end - begin;
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        state->initOriginalAndSelectedSize(n);
        ValueVector intVector(LogicalType::INT64(), mm);
        ValueVector strVector(LogicalType::STRING(), mm);
        ValueVector hashVector(LogicalType::INT64(), mm);
        intVector.state = state;
        strVector.state = state;
        hashVector.state = state;
        for (auto j = 0u; j < n; j++) {
            const auto i = begin + j;
            intVector.setNull(j, false);
            intVector.setValue<int64_t>(j, static_cast<int64_t>(i));
            strVector.setNull(j, false);
            StringVector::addString(&strVector, j, "row-" + std::to_string(i));
            hashVector.setNull(j, false);
            hashVector.setValue<uint64_t>(j, (i % 2 == 0) ? 0ULL : (1ULL << 63));
        }
        std::vector<ValueVector*> vectors{&intVector, &strVector};
        t.appendVectors(vectors, hashVector);
    };
    fill(tableA, 0, 100);   // even -> p0, odd -> p1
    fill(tableB, 100, 200); // even -> p0, odd -> p1

    // Spill B to exercise reload-on-merge of the source.
    ASSERT_EQ(tableB.spillAllPartitions(), 2u);

    tableA.merge(tableB);
    ASSERT_EQ(tableA.getNumTuples(), 200u);
    ASSERT_EQ(tableA.getPartitionNumTuples(0), 100u);
    ASSERT_EQ(tableA.getPartitionNumTuples(1), 100u);
    ASSERT_EQ(tableB.getNumTuples(), 0u); // source emptied

    // Partition 0 holds all even values 0..198 (A's first, then B's), in order.
    auto& p0 = tableA.getResidentPartition(0);
    std::vector<std::unique_ptr<Value>> valueHolders;
    std::vector<Value*> values;
    for (auto& type : columnTypes) {
        valueHolders.push_back(std::make_unique<Value>(Value::createDefaultValue(type.copy())));
        values.push_back(valueHolders.back().get());
    }
    FlatTupleIterator it(p0, values);
    std::vector<int64_t> got;
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got.push_back(values[0]->getValue<int64_t>());
    }
    ASSERT_EQ(got.size(), 100u);
    // Multiset of even values in [0, 200).
    std::sort(got.begin(), got.end());
    for (auto k = 0u; k < got.size(); k++) {
        ASSERT_EQ(got[k], static_cast<int64_t>(k * 2));
    }
}

// Verifies null flags and multiple fixed/variable-length types survive scatter + spill + reload.
TEST_F(BufferManagerTest, PartitionedFactorizedTableTypesAndNulls) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> columnTypes;
    columnTypes.push_back(LogicalType::INT64());
    columnTypes.push_back(LogicalType::STRING());
    columnTypes.push_back(LogicalType::DOUBLE());

    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_partitioned_ft_nulls.spill").string();
    PartitionedFactorizedTable partitioned(mm, LogicalType::copy(columnTypes), 1, fs, spillPath);

    const uint64_t numRows = 300;
    auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    state->initOriginalAndSelectedSize(numRows);
    ValueVector intVector(LogicalType::INT64(), mm);
    ValueVector strVector(LogicalType::STRING(), mm);
    ValueVector dblVector(LogicalType::DOUBLE(), mm);
    ValueVector hashVector(LogicalType::INT64(), mm);
    intVector.state = state;
    strVector.state = state;
    dblVector.state = state;
    hashVector.state = state;
    // Null patterns: int null when i%3==0, string null when i%4==0, double null when i%5==0.
    auto intNull = [](uint64_t i) { return i % 3 == 0; };
    auto strNull = [](uint64_t i) { return i % 4 == 0; };
    auto dblNull = [](uint64_t i) { return i % 5 == 0; };
    for (auto i = 0u; i < numRows; i++) {
        intVector.setNull(i, intNull(i));
        if (!intNull(i)) {
            intVector.setValue<int64_t>(i, static_cast<int64_t>(i));
        }
        strVector.setNull(i, strNull(i));
        if (!strNull(i)) {
            StringVector::addString(&strVector, i, "s" + std::to_string(i));
        }
        dblVector.setNull(i, dblNull(i));
        if (!dblNull(i)) {
            dblVector.setValue<double>(i, static_cast<double>(i) + 0.5);
        }
        hashVector.setNull(i, false);
        hashVector.setValue<uint64_t>(i, static_cast<uint64_t>(i % 2) << 63);
    }
    std::vector<ValueVector*> vectors{&intVector, &strVector, &dblVector};
    partitioned.appendVectors(vectors, hashVector);
    ASSERT_EQ(partitioned.getNumTuples(), numRows);
    ASSERT_EQ(partitioned.spillAllPartitions(), 2u);

    // Reload and verify each row's values and null flags (partition p holds i where i%2==p).
    uint64_t verified = 0;
    for (common::idx_t p = 0; p < 2; p++) {
        auto& table = partitioned.getResidentPartition(p);
        std::vector<std::unique_ptr<Value>> valueHolders;
        std::vector<Value*> values;
        for (auto& type : columnTypes) {
            valueHolders.push_back(std::make_unique<Value>(Value::createDefaultValue(type.copy())));
            values.push_back(valueHolders.back().get());
        }
        FlatTupleIterator it(table, values);
        uint64_t i = p;
        while (it.hasNextFlatTuple()) {
            it.getNextFlatTuple();
            ASSERT_EQ(values[0]->isNull(), intNull(i));
            if (!intNull(i)) {
                ASSERT_EQ(values[0]->getValue<int64_t>(), static_cast<int64_t>(i));
            }
            ASSERT_EQ(values[1]->isNull(), strNull(i));
            if (!strNull(i)) {
                ASSERT_EQ(values[1]->getValue<std::string>(), "s" + std::to_string(i));
            }
            ASSERT_EQ(values[2]->isNull(), dblNull(i));
            if (!dblNull(i)) {
                ASSERT_EQ(values[2]->getValue<double>(), static_cast<double>(i) + 0.5);
            }
            i += 2;
            verified++;
        }
    }
    ASSERT_EQ(verified, numRows);
}

// Sentinel build payload used to represent a null-padded (unmatched) row in a LEFT join.
static constexpr int64_t GRACE_LEFT_NULL_SENTINEL = std::numeric_limits<int64_t>::min();

// Runs a full Grace hash join (inner or left) with BOTH sides radix-partitioned and spilled to
// disk, and asserts the output multiset equals a brute-force nested-loop reference. Any mistake in
// partitioning, spilling, or probe usage disagrees with the reference. This validates the algorithm
// the eventual out-of-core HASH_JOIN operator will implement, reusing the real JoinHashTable.
static void runGraceJoinSpillTest(storage::MemoryManager* mm, common::VirtualFileSystem* fs,
    bool isLeftJoin) {
    using namespace kuzu::processor;
    using namespace kuzu::function;

    const common::idx_t logNumPartitions = 2; // 4 partitions
    const uint64_t numBuild = 2000;           // key = i % 10  -> 200 build rows per key
    const uint64_t numProbe = 1200;           // key = j % 12  -> keys 10,11 never match

    auto buildKeyFn = [](uint64_t i) { return static_cast<int64_t>(i % 10); };
    auto probeKeyFn = [](uint64_t j) { return static_cast<int64_t>(j % 12); };

    // Brute-force reference: multiset of (probeKey, buildPayload) over all matches; for LEFT join a
    // probe row with no match contributes (probeKey, sentinel).
    std::map<std::pair<int64_t, int64_t>, int64_t> expected;
    std::unordered_map<int64_t, std::vector<int64_t>> buildByKey;
    for (uint64_t i = 0; i < numBuild; i++) {
        buildByKey[buildKeyFn(i)].push_back(static_cast<int64_t>(i));
    }
    for (uint64_t j = 0; j < numProbe; j++) {
        auto it = buildByKey.find(probeKeyFn(j));
        if (it == buildByKey.end()) {
            if (isLeftJoin) {
                expected[{probeKeyFn(j), GRACE_LEFT_NULL_SENTINEL}]++;
            }
            continue;
        }
        for (auto bp : it->second) {
            expected[{probeKeyFn(j), bp}]++;
        }
    }

    // Partition build and probe by hash(key), using the same hash function the join uses.
    std::vector<LogicalType> ptTypes;
    ptTypes.push_back(LogicalType::INT64()); // key
    ptTypes.push_back(LogicalType::INT64()); // payload
    PartitionedFactorizedTable buildParts(mm, LogicalType::copy(ptTypes), logNumPartitions, fs,
        (std::filesystem::temp_directory_path() / "kuzu_grace_build.spill").string());
    PartitionedFactorizedTable probeParts(mm, LogicalType::copy(ptTypes), logNumPartitions, fs,
        (std::filesystem::temp_directory_path() / "kuzu_grace_probe.spill").string());

    auto partitionInput = [&](PartitionedFactorizedTable& parts, uint64_t n, auto keyFn) {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm);
        ValueVector payVec(LogicalType::INT64(), mm);
        ValueVector hashVec(LogicalType::HASH(), mm);
        keyVec.state = state;
        payVec.state = state;
        hashVec.state = state;
        for (uint64_t b = 0; b < n; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, n - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, keyFn(b + r));
                payVec.setNull(r, false);
                payVec.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            VectorHashFunction::computeHash(keyVec, state->getSelVector(), hashVec,
                state->getSelVector());
            std::vector<ValueVector*> vs{&keyVec, &payVec};
            parts.appendVectors(vs, hashVec);
        }
    };
    partitionInput(buildParts, numBuild, buildKeyFn);
    partitionInput(probeParts, numProbe, probeKeyFn);

    // Force both sides to disk (simulate memory pressure between build and probe).
    buildParts.spillAllPartitions();
    probeParts.spillAllPartitions();

    // JoinHashTable layout: [key(INT64), payload(INT64), hash(HASH), prevPtr(INT64)].
    auto makeJHTSchema = []() {
        FactorizedTableSchema s;
        s.appendColumn(ColumnSchema(false /*isUnFlat*/, 0 /*groupID*/,
            LogicalTypeUtils::getRowLayoutSize(LogicalType::INT64())));
        s.appendColumn(
            ColumnSchema(false, 0, LogicalTypeUtils::getRowLayoutSize(LogicalType::INT64())));
        s.appendColumn(ColumnSchema(false, INVALID_DATA_CHUNK_POS,
            LogicalTypeUtils::getRowLayoutSize(LogicalType::HASH())));
        s.appendColumn(ColumnSchema(false, INVALID_DATA_CHUNK_POS,
            LogicalTypeUtils::getRowLayoutSize(LogicalType::INT64())));
        return s;
    };

    std::map<std::pair<int64_t, int64_t>, int64_t> got;

    auto probeKeyState = DataChunkState::getSingleValueDataChunkState();
    ValueVector probeKeyVec(LogicalType::INT64(), mm);
    probeKeyVec.state = probeKeyState;
    ValueVector jhtHashVec(LogicalType::HASH(), mm); // scratch for probe hashing
    SelectionVector hashSelVec(DEFAULT_VECTOR_CAPACITY);
    auto probedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);
    auto matchedTuples = std::make_unique<uint8_t*[]>(DEFAULT_VECTOR_CAPACITY);

    for (common::idx_t p = 0; p < buildParts.getNumPartitions(); p++) {
        // Build a JoinHashTable from the (reloaded) build partition p.
        std::vector<LogicalType> jhtKeyTypes;
        jhtKeyTypes.push_back(LogicalType::INT64());
        auto jht = std::make_unique<JoinHashTable>(*mm, std::move(jhtKeyTypes), makeJHTSchema());
        auto& buildPart = buildParts.getResidentPartition(p);
        if (buildPart.getNumTuples() > 0) {
            auto scanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
            ValueVector bKey(LogicalType::INT64(), mm);
            ValueVector bPay(LogicalType::INT64(), mm);
            bKey.state = scanState;
            bPay.state = scanState;
            std::vector<ValueVector*> scanVecs{&bKey, &bPay};
            for (uint64_t t = 0; t < buildPart.getNumTuples(); t += DEFAULT_VECTOR_CAPACITY) {
                const auto m =
                    std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, buildPart.getNumTuples() - t);
                scanState->initOriginalAndSelectedSize(m);
                buildPart.scan(std::span<ValueVector*>(scanVecs), t, m);
                jht->appendVectors({&bKey}, {&bPay}, scanState.get());
            }
            jht->allocateHashSlots(jht->getNumEntries());
            jht->buildHashSlots();
        }

        auto& probePart = probeParts.getResidentPartition(p);
        if (probePart.getNumTuples() == 0) {
            continue;
        }
        const auto payloadColOffset = jht->getTableSchema()->getColOffset(1);
        auto pScanState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector pKey(LogicalType::INT64(), mm);
        ValueVector pPay(LogicalType::INT64(), mm);
        pKey.state = pScanState;
        pPay.state = pScanState;
        std::vector<ValueVector*> pScanVecs{&pKey, &pPay};
        for (uint64_t t = 0; t < probePart.getNumTuples(); t += DEFAULT_VECTOR_CAPACITY) {
            const auto m =
                std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, probePart.getNumTuples() - t);
            pScanState->initOriginalAndSelectedSize(m);
            probePart.scan(std::span<ValueVector*>(pScanVecs), t, m);
            for (uint64_t r = 0; r < m; r++) {
                const int64_t probeKey = pKey.getValue<int64_t>(r);
                probeKeyVec.setNull(0, false);
                probeKeyVec.setValue<int64_t>(0, probeKey);
                probeKeyState->getSelVectorUnsafe().setToUnfiltered(1);
                probedTuples[0] = nullptr;
                uint64_t rowMatches = 0;
                jht->probe({&probeKeyVec}, jhtHashVec, hashSelVec, nullptr, probedTuples.get());
                while (probedTuples[0] != nullptr) {
                    const auto numMatched =
                        jht->matchFlatKeys({&probeKeyVec}, probedTuples.get(), matchedTuples.get());
                    for (common::sel_t mi = 0; mi < numMatched; mi++) {
                        const int64_t buildPayload =
                            *reinterpret_cast<int64_t*>(matchedTuples[mi] + payloadColOffset);
                        got[{probeKey, buildPayload}]++;
                        rowMatches++;
                    }
                    if (numMatched < DEFAULT_VECTOR_CAPACITY) {
                        break; // chain fully walked
                    }
                }
                if (isLeftJoin && rowMatches == 0) {
                    got[{probeKey, GRACE_LEFT_NULL_SENTINEL}]++;
                }
            }
        }
    }

    ASSERT_EQ(got.size(), expected.size());
    ASSERT_TRUE(got == expected) << "Grace join output multiset differs from brute-force reference";
}

// Full out-of-core INNER hash join proven correct against a brute-force reference (see helper).
TEST_F(BufferManagerTest, GraceInnerJoinWithSpilling) {
    runGraceJoinSpillTest(getMemoryManager(*database), getFileSystem(*database),
        false /*isLeftJoin*/);
}

// Full out-of-core LEFT-OUTER hash join: matches plus null-padded rows for non-matching probe keys,
// all under forced spilling, proven correct against a brute-force reference.
TEST_F(BufferManagerTest, GraceLeftJoinWithSpilling) {
    runGraceJoinSpillTest(getMemoryManager(*database), getFileSystem(*database),
        true /*isLeftJoin*/);
}

// Exercises the production GraceHashJoinExecutor end to end under a tiny memory budget that forces
// partitions to spill during append: feed build + probe, materialize the join (inner or left), and
// compare the output multiset to a brute-force reference. A left join's null-padded build payload
// is represented by the sentinel in the multiset.
static void runGraceExecutorTest(storage::MemoryManager* mm, common::VirtualFileSystem* fs,
    bool isLeftJoin) {
    using namespace kuzu::processor;

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> buildPayloadTypes;
    buildPayloadTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> probePayloadTypes;
    probePayloadTypes.push_back(LogicalType::INT64());

    // A 4 KiB budget is far below a partition's size, so spillToReduceResidentBytesTo spills during
    // append -- exercising the out-of-core path.
    GraceHashJoinExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_ghje_build.spill").string(),
        (std::filesystem::temp_directory_path() / "kuzu_ghje_probe.spill").string(),
        LogicalType::copy(keyTypes), LogicalType::copy(buildPayloadTypes),
        LogicalType::copy(probePayloadTypes), 2 /*logNumPartitions*/, 4096 /*memoryBudgetBytes*/);

    const uint64_t numBuild = 2000, numProbe = 1200;
    auto buildKeyFn = [](uint64_t i) { return static_cast<int64_t>(i % 10); };
    auto probeKeyFn = [](uint64_t j) { return static_cast<int64_t>(j % 12); };

    auto feed = [&](bool isBuild, uint64_t n, auto keyFn) {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm);
        ValueVector payVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        payVec.state = state;
        for (uint64_t b = 0; b < n; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, n - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, keyFn(b + r));
                payVec.setNull(r, false);
                payVec.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            if (isBuild) {
                exec.appendBuild({&keyVec}, {&payVec});
            } else {
                exec.appendProbe({&keyVec}, {&payVec});
            }
        }
    };
    feed(true, numBuild, buildKeyFn);
    feed(false, numProbe, probeKeyFn);

    auto output = isLeftJoin ? exec.computeLeftJoin() : exec.computeInnerJoin();

    // Brute-force reference: multiset of (probeKey, buildPayload); left join adds (probeKey,
    // sentinel) for non-matching probe rows.
    std::map<std::pair<int64_t, int64_t>, int64_t> expected;
    std::unordered_map<int64_t, std::vector<int64_t>> buildByKey;
    for (uint64_t i = 0; i < numBuild; i++) {
        buildByKey[buildKeyFn(i)].push_back(static_cast<int64_t>(i));
    }
    for (uint64_t j = 0; j < numProbe; j++) {
        auto it = buildByKey.find(probeKeyFn(j));
        if (it == buildByKey.end()) {
            if (isLeftJoin) {
                expected[{probeKeyFn(j), GRACE_LEFT_NULL_SENTINEL}]++;
            }
            continue;
        }
        for (auto bp : it->second) {
            expected[{probeKeyFn(j), bp}]++;
        }
    }

    // Output columns: [probeKey, probePayload, buildPayload]; compare (probeKey, buildPayload) with
    // a null build payload mapped to the sentinel.
    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<std::pair<int64_t, int64_t>, int64_t> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        const int64_t buildPayload =
            values[2]->isNull() ? GRACE_LEFT_NULL_SENTINEL : values[2]->getValue<int64_t>();
        got[{values[0]->getValue<int64_t>(), buildPayload}]++;
    }

    ASSERT_EQ(got.size(), expected.size());
    ASSERT_TRUE(got == expected)
        << "GraceHashJoinExecutor output multiset differs from brute-force reference";
}

TEST_F(BufferManagerTest, GraceHashJoinExecutorInnerJoin) {
    runGraceExecutorTest(getMemoryManager(*database), getFileSystem(*database),
        false /*isLeftJoin*/);
}

TEST_F(BufferManagerTest, GraceHashJoinExecutorLeftJoin) {
    runGraceExecutorTest(getMemoryManager(*database), getFileSystem(*database),
        true /*isLeftJoin*/);
}

// Same forced-spill scenario as runGraceExecutorTest, but drives the resumable streaming probe
// (initProbeStream + getNextChunk) instead of materializing the whole join, and checks the streamed
// output multiset against the same brute-force reference. Each streamed chunk is one probe row (flat)
// times its matched build payloads (unflat), so we expand the factorized chunk back into rows. This
// is the exact emission the live HASH_JOIN operator will consume.
static void runGraceStreamTest(storage::MemoryManager* mm, common::VirtualFileSystem* fs,
    bool isLeftJoin) {
    using namespace kuzu::processor;

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> buildPayloadTypes;
    buildPayloadTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> probePayloadTypes;
    probePayloadTypes.push_back(LogicalType::INT64());

    GraceHashJoinExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_ghjs_build.spill").string(),
        (std::filesystem::temp_directory_path() / "kuzu_ghjs_probe.spill").string(),
        LogicalType::copy(keyTypes), LogicalType::copy(buildPayloadTypes),
        LogicalType::copy(probePayloadTypes), 2 /*logNumPartitions*/, 4096 /*memoryBudgetBytes*/);

    const uint64_t numBuild = 2000, numProbe = 1200;
    auto buildKeyFn = [](uint64_t i) { return static_cast<int64_t>(i % 10); };
    auto probeKeyFn = [](uint64_t j) { return static_cast<int64_t>(j % 12); };

    auto feed = [&](bool isBuild, uint64_t n, auto keyFn) {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm);
        ValueVector payVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        payVec.state = state;
        for (uint64_t b = 0; b < n; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, n - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, keyFn(b + r));
                payVec.setNull(r, false);
                payVec.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            if (isBuild) {
                exec.appendBuild({&keyVec}, {&payVec});
            } else {
                exec.appendProbe({&keyVec}, {&payVec});
            }
        }
    };
    feed(true, numBuild, buildKeyFn);
    feed(false, numProbe, probeKeyFn);

    // Output vectors: probe columns flat (single-value state), build payload unflat.
    auto flatState = DataChunkState::getSingleValueDataChunkState();
    auto buildState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    ValueVector probeKeyOut(LogicalType::INT64(), mm);
    ValueVector probePayOut(LogicalType::INT64(), mm);
    ValueVector buildPayOut(LogicalType::INT64(), mm);
    probeKeyOut.state = flatState;
    probePayOut.state = flatState;
    buildPayOut.state = buildState;

    exec.initProbeStream({&probeKeyOut, &probePayOut}, {&buildPayOut}, isLeftJoin);

    std::map<std::pair<int64_t, int64_t>, int64_t> got;
    while (exec.getNextChunk()) {
        const auto probePos = probeKeyOut.state->getSelVector()[0];
        const int64_t probeKey = probeKeyOut.getValue<int64_t>(probePos);
        const auto& bsel = buildState->getSelVector();
        for (auto k = 0u; k < bsel.getSelSize(); k++) {
            const auto bpos = bsel[k];
            const int64_t buildPayload = buildPayOut.isNull(bpos) ?
                                             GRACE_LEFT_NULL_SENTINEL :
                                             buildPayOut.getValue<int64_t>(bpos);
            got[{probeKey, buildPayload}]++;
        }
    }

    // Brute-force reference (identical to runGraceExecutorTest).
    std::map<std::pair<int64_t, int64_t>, int64_t> expected;
    std::unordered_map<int64_t, std::vector<int64_t>> buildByKey;
    for (uint64_t i = 0; i < numBuild; i++) {
        buildByKey[buildKeyFn(i)].push_back(static_cast<int64_t>(i));
    }
    for (uint64_t j = 0; j < numProbe; j++) {
        auto it = buildByKey.find(probeKeyFn(j));
        if (it == buildByKey.end()) {
            if (isLeftJoin) {
                expected[{probeKeyFn(j), GRACE_LEFT_NULL_SENTINEL}]++;
            }
            continue;
        }
        for (auto bp : it->second) {
            expected[{probeKeyFn(j), bp}]++;
        }
    }

    ASSERT_EQ(got.size(), expected.size());
    ASSERT_TRUE(got == expected)
        << "GraceHashJoinExecutor streamed output multiset differs from brute-force reference";
}

// Reproduction: the join key is ALSO materialized as a build payload (same vector passed as both
// key and payload), as happens when a query returns the build-side join key. Every output row's
// key column must equal its duplicated-key payload column.
TEST_F(BufferManagerTest, GraceHashJoinExecutorKeyAsPayload) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);
    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> buildPayloadTypes; // [dup-of-key INT64, tag INT64]
    buildPayloadTypes.push_back(LogicalType::INT64());
    buildPayloadTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> probePayloadTypes;
    probePayloadTypes.push_back(LogicalType::INT64());

    GraceHashJoinExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_ghjk_build.spill").string(),
        (std::filesystem::temp_directory_path() / "kuzu_ghjk_probe.spill").string(),
        LogicalType::copy(keyTypes), LogicalType::copy(buildPayloadTypes),
        LogicalType::copy(probePayloadTypes), 2, 1u << 30 /*no spill*/);

    const uint64_t numBuild = 1000, numProbe = 500;
    auto keyFn = [](uint64_t i) { return static_cast<int64_t>(i % 50); };
    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector key(LogicalType::INT64(), mm), tag(LogicalType::INT64(), mm);
        key.state = state;
        tag.state = state;
        for (uint64_t b = 0; b < numBuild; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numBuild - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                key.setValue<int64_t>(r, keyFn(b + r));
                tag.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            // key vector passed as key AND as the first payload.
            exec.appendBuild({&key}, {&key, &tag});
        }
    }
    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector key(LogicalType::INT64(), mm), ppay(LogicalType::INT64(), mm);
        key.state = state;
        ppay.state = state;
        for (uint64_t b = 0; b < numProbe; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numProbe - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                key.setValue<int64_t>(r, keyFn(b + r));
                ppay.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            exec.appendProbe({&key}, {&ppay});
        }
    }
    auto output = exec.computeInnerJoin();
    // Output columns: [probeKey, probePayload, buildKeyDup, buildTag]. probeKey must equal the
    // duplicated build key column in every row.
    std::vector<LogicalType> outTypes;
    for (int i = 0; i < 4; i++) {
        outTypes.push_back(LogicalType::INT64());
    }
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    uint64_t rows = 0;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        ASSERT_EQ(values[0]->getValue<int64_t>(), values[2]->getValue<int64_t>())
            << "probeKey != buildKeyDup at row " << rows;
        rows++;
    }
    ASSERT_GT(rows, 0u);
}

TEST_F(BufferManagerTest, GraceHashJoinExecutorStreamInnerJoin) {
    runGraceStreamTest(getMemoryManager(*database), getFileSystem(*database), false /*isLeftJoin*/);
}

TEST_F(BufferManagerTest, GraceHashJoinExecutorStreamLeftJoin) {
    runGraceStreamTest(getMemoryManager(*database), getFileSystem(*database), true /*isLeftJoin*/);
}

// Composite (2-column) join key + a variable-length (STRING) build payload, under forced spilling.
// Validates multi-key co-partitioning (the routing hash matches JoinHashTable's internal multi-key
// hash) and that string payloads survive spill/reload/lookup/append.
TEST_F(BufferManagerTest, GraceHashJoinExecutorCompositeKeyStringPayload) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> buildPayloadTypes;
    buildPayloadTypes.push_back(LogicalType::STRING());
    std::vector<LogicalType> probePayloadTypes;
    probePayloadTypes.push_back(LogicalType::INT64());

    GraceHashJoinExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_ghje_ck_build.spill").string(),
        (std::filesystem::temp_directory_path() / "kuzu_ghje_ck_probe.spill").string(),
        LogicalType::copy(keyTypes), LogicalType::copy(buildPayloadTypes),
        LogicalType::copy(probePayloadTypes), 2 /*logNumPartitions*/, 8192 /*memoryBudgetBytes*/);

    const uint64_t numBuild = 1400, numProbe = 900;
    auto bK1 = [](uint64_t i) { return static_cast<int64_t>(i % 7); };
    auto bK2 = [](uint64_t i) { return static_cast<int64_t>(i % 3); };
    auto pK1 = [](uint64_t j) { return static_cast<int64_t>(j % 7); };
    auto pK2 = [](uint64_t j) { return static_cast<int64_t>(j % 4); }; // k2==3 never matches build
    auto buildStr = [](uint64_t i) { return "b" + std::to_string(i); };

    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector k1(LogicalType::INT64(), mm), k2(LogicalType::INT64(), mm),
            pay(LogicalType::STRING(), mm);
        k1.state = state;
        k2.state = state;
        pay.state = state;
        for (uint64_t b = 0; b < numBuild; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numBuild - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                k1.setNull(r, false);
                k1.setValue<int64_t>(r, bK1(b + r));
                k2.setNull(r, false);
                k2.setValue<int64_t>(r, bK2(b + r));
                pay.setNull(r, false);
                StringVector::addString(&pay, r, buildStr(b + r));
            }
            exec.appendBuild({&k1, &k2}, {&pay});
        }
    }
    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector k1(LogicalType::INT64(), mm), k2(LogicalType::INT64(), mm),
            pay(LogicalType::INT64(), mm);
        k1.state = state;
        k2.state = state;
        pay.state = state;
        for (uint64_t b = 0; b < numProbe; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numProbe - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                k1.setNull(r, false);
                k1.setValue<int64_t>(r, pK1(b + r));
                k2.setNull(r, false);
                k2.setValue<int64_t>(r, pK2(b + r));
                pay.setNull(r, false);
                pay.setValue<int64_t>(r, static_cast<int64_t>(b + r));
            }
            exec.appendProbe({&k1, &k2}, {&pay});
        }
    }

    auto output = exec.computeInnerJoin();

    // Brute-force reference: multiset of (probeK1, probeK2, buildStr).
    std::map<std::tuple<int64_t, int64_t, std::string>, int64_t> expected;
    std::map<std::pair<int64_t, int64_t>, std::vector<std::string>> buildByKey;
    for (uint64_t i = 0; i < numBuild; i++) {
        buildByKey[{bK1(i), bK2(i)}].push_back(buildStr(i));
    }
    for (uint64_t j = 0; j < numProbe; j++) {
        auto it = buildByKey.find({pK1(j), pK2(j)});
        if (it == buildByKey.end()) {
            continue;
        }
        for (auto& s : it->second) {
            expected[{pK1(j), pK2(j), s}]++;
        }
    }

    // Output columns: [k1, k2, probePayload(INT64), buildStr(STRING)]; compare (k1, k2, buildStr).
    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::STRING());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<std::tuple<int64_t, int64_t, std::string>, int64_t> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got[{values[0]->getValue<int64_t>(), values[1]->getValue<int64_t>(),
            values[3]->getValue<std::string>()}]++;
    }

    ASSERT_EQ(got.size(), expected.size());
    ASSERT_TRUE(got == expected)
        << "Composite-key/string-payload Grace join differs from brute-force reference";
}

// Builds COUNT(*) and SUM(INT64) aggregate functions without going through the binder/catalog, so
// the executor can be exercised in isolation. COUNT_STAR is taken from its function set (which sets
// needToHandleNulls); SUM(INT64->INT64) is constructed directly from the concrete SumFunction so no
// type-widening bind step is needed.
static std::vector<function::AggregateFunction> makeCountSumAggFuncs() {
    using namespace kuzu::function;
    std::vector<AggregateFunction> aggFuncs;
    auto countStarSet = CountStarFunction::getFunctionSet();
    aggFuncs.push_back(
        common::ku_dynamic_cast<AggregateFunction*>(countStarSet[0].get())->copy());
    aggFuncs.push_back(AggregateFunctionUtils::getAggFunc<SumFunction<int64_t, int64_t>>("SUM",
        common::LogicalTypeID::INT64, common::LogicalTypeID::INT64, false /*isDistinct*/)
                           ->copy());
    return aggFuncs;
}

// Exercises the out-of-core PartitionedAggregateExecutor: feed rows of (groupKey, value), then
// GROUP BY groupKey computing COUNT(*) and SUM(value). Runs once with a tiny budget (forcing raw
// input rows to spill and re-load during append) and once with a large budget (fully in memory), and
// checks both against the same brute-force reference. A STRING group key additionally exercises
// overflow serialization of the spilled raw rows. `intKey` picks an INT64 vs STRING group key.
static void runPartitionedAggTest(storage::MemoryManager* mm, common::VirtualFileSystem* fs,
    uint64_t budget, bool intKey) {
    using namespace kuzu::processor;
    using namespace kuzu::function;

    const uint64_t numRows = 5000;
    const int64_t numGroups = 20;
    auto groupOf = [&](uint64_t i) { return static_cast<int64_t>(i % numGroups); };
    auto valOf = [&](uint64_t i) { return static_cast<int64_t>((i * 7) % 100); };
    auto keyStr = [&](int64_t g) { return "g" + std::to_string(g); };

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(intKey ? LogicalType::INT64() : LogicalType::STRING());
    // Per-function input type: ANY() for COUNT(*), INT64 for SUM. Result types: INT64, INT64.
    std::vector<LogicalType> aggInputTypes;
    aggInputTypes.push_back(LogicalType::ANY());
    aggInputTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> aggResultTypes;
    aggResultTypes.push_back(LogicalType::INT64());
    aggResultTypes.push_back(LogicalType::INT64());

    PartitionedAggregateExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_pae.spill").string(),
        LogicalType::copy(keyTypes), std::vector<LogicalType>{} /*dependentKeyTypes*/,
        makeCountSumAggFuncs(), LogicalType::copy(aggInputTypes), LogicalType::copy(aggResultTypes),
        2 /*logNumPartitions*/, budget);

    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(keyTypes[0].copy(), mm);
        ValueVector valVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        valVec.state = state;
        for (uint64_t b = 0; b < numRows; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numRows - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                const auto g = groupOf(b + r);
                keyVec.setNull(r, false);
                if (intKey) {
                    keyVec.setValue<int64_t>(r, g);
                } else {
                    StringVector::addString(&keyVec, r, keyStr(g));
                }
                valVec.setNull(r, false);
                valVec.setValue<int64_t>(r, valOf(b + r));
            }
            // aggInputVectors: [nullptr for COUNT(*), &valVec for SUM].
            exec.append({&keyVec}, {} /*dependentKeyVectors*/, {nullptr, &valVec});
        }
    }

    auto output = exec.computeAggregates();

    // Brute-force reference: per group, (count, sum).
    std::map<int64_t, std::pair<int64_t, int64_t>> expected;
    for (uint64_t i = 0; i < numRows; i++) {
        auto& e = expected[groupOf(i)];
        e.first += 1;
        e.second += valOf(i);
    }

    // Output columns: [key, count(INT64), sum(INT64)].
    std::vector<LogicalType> outTypes;
    outTypes.push_back(keyTypes[0].copy());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<int64_t, std::pair<int64_t, int64_t>> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        int64_t g = intKey ?
                        values[0]->getValue<int64_t>() :
                        static_cast<int64_t>(std::stoll(values[0]->getValue<std::string>().substr(1)));
        got[g] = {values[1]->getValue<int64_t>(), values[2]->getValue<int64_t>()};
    }

    ASSERT_EQ(got.size(), static_cast<size_t>(numGroups));
    ASSERT_TRUE(got == expected)
        << "PartitionedAggregateExecutor result differs from brute-force reference";
}

TEST_F(BufferManagerTest, PartitionedAggregateExecutorSpillIntKey) {
    // 4 KiB budget << 5000 rows: forces raw input rows to spill/reload during append.
    runPartitionedAggTest(getMemoryManager(*database), getFileSystem(*database), 4096 /*budget*/,
        true /*intKey*/);
}

TEST_F(BufferManagerTest, PartitionedAggregateExecutorNoSpillIntKey) {
    // Huge budget: everything stays in memory; must match the spilling run's brute-force reference.
    runPartitionedAggTest(getMemoryManager(*database), getFileSystem(*database),
        1ull << 30 /*budget*/, true /*intKey*/);
}

TEST_F(BufferManagerTest, PartitionedAggregateExecutorSpillStringKey) {
    // STRING group key under a tiny budget also exercises overflow serialization of spilled rows.
    runPartitionedAggTest(getMemoryManager(*database), getFileSystem(*database), 4096 /*budget*/,
        false /*intKey*/);
}

// Picks the non-distinct MIN(STRING) aggregate function out of its function set. The min/max function
// set entries are fully concrete (the comparison op is baked in via template), so no bind step is
// needed.
static function::AggregateFunction makeMinStringAggFunc() {
    using namespace kuzu::function;
    auto set = AggregateMinFunction::getFunctionSet();
    for (auto& f : set) {
        auto* af = common::ku_dynamic_cast<AggregateFunction*>(f.get());
        if (!af->isFunctionDistinct() && af->parameterTypeIDs.size() == 1 &&
            af->parameterTypeIDs[0] == common::LogicalTypeID::STRING) {
            return af->copy();
        }
    }
    throw std::runtime_error("MIN(STRING) not found in function set");
}

// Proves the headline correctness claim: a *stateful* aggregate whose state carries an overflow
// pointer (MIN(STRING)) is computed correctly under forced spilling, because aggregate states never
// leave memory -- only the raw STRING input column spills (and round-trips via serialization), while
// each partition is aggregated by a fresh in-memory table.
TEST_F(BufferManagerTest, PartitionedAggregateExecutorMinStringSpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    const uint64_t numRows = 5000;
    const int64_t numGroups = 15;
    auto groupOf = [&](uint64_t i) { return static_cast<int64_t>(i % numGroups); };
    auto strOf = [&](uint64_t i) { return "s" + std::to_string((i * 13) % 40); };

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<function::AggregateFunction> aggFuncs;
    aggFuncs.push_back(makeMinStringAggFunc());
    std::vector<LogicalType> aggInputTypes;
    aggInputTypes.push_back(LogicalType::STRING());
    std::vector<LogicalType> aggResultTypes;
    aggResultTypes.push_back(LogicalType::STRING());

    PartitionedAggregateExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_pae_minstr.spill").string(),
        LogicalType::copy(keyTypes), std::vector<LogicalType>{} /*dependentKeyTypes*/,
        std::move(aggFuncs), LogicalType::copy(aggInputTypes), LogicalType::copy(aggResultTypes),
        2 /*logNumPartitions*/, 4096 /*budget forces spill*/);

    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm);
        ValueVector sVec(LogicalType::STRING(), mm);
        keyVec.state = state;
        sVec.state = state;
        for (uint64_t b = 0; b < numRows; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numRows - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, groupOf(b + r));
                sVec.setNull(r, false);
                StringVector::addString(&sVec, r, strOf(b + r));
            }
            exec.append({&keyVec}, {} /*dependentKeyVectors*/, {&sVec});
        }
    }

    auto output = exec.computeAggregates();

    // Brute-force reference: per group, lexicographic min string (ASCII, so std::string < matches
    // Kuzu's byte-wise comparison).
    std::map<int64_t, std::string> expected;
    for (uint64_t i = 0; i < numRows; i++) {
        auto g = groupOf(i);
        auto s = strOf(i);
        auto it = expected.find(g);
        if (it == expected.end() || s < it->second) {
            expected[g] = s;
        }
    }

    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::STRING());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<int64_t, std::string> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got[values[0]->getValue<int64_t>()] = values[1]->getValue<std::string>();
    }

    ASSERT_EQ(got.size(), static_cast<size_t>(numGroups));
    ASSERT_TRUE(got == expected)
        << "MIN(STRING) under spilling differs from brute-force reference";
}

// Exercises dependent (payload) keys: GROUP BY an INT64 key while carrying a STRING column that is
// functionally dependent on the key (same value per group). The dependent key must be stored, spilled
// with the raw rows, and re-emitted unchanged alongside COUNT(*)/SUM.
TEST_F(BufferManagerTest, PartitionedAggregateExecutorDependentKeySpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    const uint64_t numRows = 5000;
    const int64_t numGroups = 12;
    auto groupOf = [&](uint64_t i) { return static_cast<int64_t>(i % numGroups); };
    auto depOf = [&](int64_t g) { return "grp" + std::to_string(g); };
    auto valOf = [&](uint64_t i) { return static_cast<int64_t>((i * 3) % 100); };

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> dependentKeyTypes;
    dependentKeyTypes.push_back(LogicalType::STRING());
    std::vector<LogicalType> aggInputTypes;
    aggInputTypes.push_back(LogicalType::ANY()); // COUNT(*)
    aggInputTypes.push_back(LogicalType::INT64()); // SUM
    std::vector<LogicalType> aggResultTypes;
    aggResultTypes.push_back(LogicalType::INT64());
    aggResultTypes.push_back(LogicalType::INT64());

    PartitionedAggregateExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_pae_dep.spill").string(),
        LogicalType::copy(keyTypes), LogicalType::copy(dependentKeyTypes), makeCountSumAggFuncs(),
        LogicalType::copy(aggInputTypes), LogicalType::copy(aggResultTypes), 2 /*logNumPartitions*/,
        4096 /*budget forces spill*/);

    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm), depVec(LogicalType::STRING(), mm),
            valVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        depVec.state = state;
        valVec.state = state;
        for (uint64_t b = 0; b < numRows; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numRows - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                const auto g = groupOf(b + r);
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, g);
                depVec.setNull(r, false);
                StringVector::addString(&depVec, r, depOf(g));
                valVec.setNull(r, false);
                valVec.setValue<int64_t>(r, valOf(b + r));
            }
            exec.append({&keyVec}, {&depVec}, {nullptr, &valVec});
        }
    }

    auto output = exec.computeAggregates();

    std::map<int64_t, std::tuple<std::string, int64_t, int64_t>> expected;
    for (uint64_t i = 0; i < numRows; i++) {
        const auto g = groupOf(i);
        auto& e = expected[g];
        std::get<0>(e) = depOf(g);
        std::get<1>(e) += 1;
        std::get<2>(e) += valOf(i);
    }

    // Output columns: [key(INT64), dep(STRING), count(INT64), sum(INT64)].
    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::STRING());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<int64_t, std::tuple<std::string, int64_t, int64_t>> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got[values[0]->getValue<int64_t>()] = {values[1]->getValue<std::string>(),
            values[2]->getValue<int64_t>(), values[3]->getValue<int64_t>()};
    }

    ASSERT_EQ(got.size(), static_cast<size_t>(numGroups));
    ASSERT_TRUE(got == expected)
        << "Dependent-key aggregation under spilling differs from brute-force reference";
}

// Exercises per-row multiplicity: different append batches carry different multiplicities, so within
// a partition (which preserves append order) rows form contiguous runs of mixed multiplicity that the
// executor must re-apply. COUNT(*) must sum the multiplicities and SUM must scale by them.
TEST_F(BufferManagerTest, PartitionedAggregateExecutorMultiplicitySpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    const uint64_t numRows = 6000;
    const int64_t numGroups = 10;
    auto groupOf = [&](uint64_t i) { return static_cast<int64_t>(i % numGroups); };
    auto valOf = [&](uint64_t i) { return static_cast<int64_t>((i * 9) % 50); };
    // Multiplicity alternates per batch so partitions see runs of mixed multiplicity.
    auto multOf = [&](uint64_t batchIdx) { return static_cast<uint64_t>(batchIdx % 2 == 0 ? 1 : 3); };

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> aggInputTypes;
    aggInputTypes.push_back(LogicalType::ANY()); // COUNT(*)
    aggInputTypes.push_back(LogicalType::INT64()); // SUM
    std::vector<LogicalType> aggResultTypes;
    aggResultTypes.push_back(LogicalType::INT64());
    aggResultTypes.push_back(LogicalType::INT64());

    PartitionedAggregateExecutor exec(mm, fs,
        (std::filesystem::temp_directory_path() / "kuzu_pae_mult.spill").string(),
        LogicalType::copy(keyTypes), std::vector<LogicalType>{} /*dependentKeyTypes*/,
        makeCountSumAggFuncs(), LogicalType::copy(aggInputTypes), LogicalType::copy(aggResultTypes),
        2 /*logNumPartitions*/, 4096 /*budget forces spill*/);

    std::map<int64_t, std::pair<int64_t, int64_t>> expected; // group -> (count, sum)
    {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm), valVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        valVec.state = state;
        uint64_t batchIdx = 0;
        for (uint64_t b = 0; b < numRows; b += DEFAULT_VECTOR_CAPACITY, batchIdx++) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, numRows - b);
            const auto mult = multOf(batchIdx);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                const auto g = groupOf(b + r);
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, g);
                valVec.setNull(r, false);
                valVec.setValue<int64_t>(r, valOf(b + r));
                auto& e = expected[g];
                e.first += static_cast<int64_t>(mult);
                e.second += valOf(b + r) * static_cast<int64_t>(mult);
            }
            exec.append({&keyVec}, {} /*dependentKeyVectors*/, {nullptr, &valVec}, mult);
        }
    }

    auto output = exec.computeAggregates();

    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<int64_t, std::pair<int64_t, int64_t>> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got[values[0]->getValue<int64_t>()] = {values[1]->getValue<int64_t>(),
            values[2]->getValue<int64_t>()};
    }

    ASSERT_EQ(got.size(), static_cast<size_t>(numGroups));
    ASSERT_TRUE(got == expected)
        << "Multiplicity-weighted aggregation under spilling differs from brute-force reference";
}

// Exercises merging two per-thread executors: feed overlapping groups to two independently-spilling
// executors, merge one into the other, and check the combined aggregation. This is the primitive a
// multi-threaded spilling HashAggregate uses to combine per-thread partitions before finalization.
TEST_F(BufferManagerTest, PartitionedAggregateExecutorMergeSpill) {
    using namespace kuzu::processor;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    const int64_t numGroups = 15;
    auto groupOf = [&](uint64_t i) { return static_cast<int64_t>(i % numGroups); };
    auto valOf = [&](uint64_t i) { return static_cast<int64_t>((i * 5) % 80); };

    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> aggInputTypes;
    aggInputTypes.push_back(LogicalType::ANY()); // COUNT(*)
    aggInputTypes.push_back(LogicalType::INT64()); // SUM
    std::vector<LogicalType> aggResultTypes;
    aggResultTypes.push_back(LogicalType::INT64());
    aggResultTypes.push_back(LogicalType::INT64());

    auto makeExec = [&](const std::string& stem) {
        return std::make_unique<PartitionedAggregateExecutor>(mm, fs,
            (std::filesystem::temp_directory_path() / stem).string(), LogicalType::copy(keyTypes),
            std::vector<LogicalType>{}, makeCountSumAggFuncs(), LogicalType::copy(aggInputTypes),
            LogicalType::copy(aggResultTypes), 2 /*logNumPartitions*/, 4096 /*budget forces spill*/);
    };
    auto exec1 = makeExec("kuzu_pae_merge1.spill");
    auto exec2 = makeExec("kuzu_pae_merge2.spill");

    std::map<int64_t, std::pair<int64_t, int64_t>> expected; // group -> (count, sum)
    auto feed = [&](PartitionedAggregateExecutor& exec, uint64_t base, uint64_t n) {
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        ValueVector keyVec(LogicalType::INT64(), mm), valVec(LogicalType::INT64(), mm);
        keyVec.state = state;
        valVec.state = state;
        for (uint64_t b = 0; b < n; b += DEFAULT_VECTOR_CAPACITY) {
            const auto m = std::min<uint64_t>(DEFAULT_VECTOR_CAPACITY, n - b);
            state->initOriginalAndSelectedSize(m);
            for (uint64_t r = 0; r < m; r++) {
                const auto idx = base + b + r;
                const auto g = groupOf(idx);
                keyVec.setNull(r, false);
                keyVec.setValue<int64_t>(r, g);
                valVec.setNull(r, false);
                valVec.setValue<int64_t>(r, valOf(idx));
                auto& e = expected[g];
                e.first += 1;
                e.second += valOf(idx);
            }
            exec.append({&keyVec}, {}, {nullptr, &valVec});
        }
    };
    // Overlapping group spaces (both use i % numGroups), distinct value indices.
    feed(*exec1, 0, 3000);
    feed(*exec2, 100000, 2500);

    exec1->merge(*exec2);
    auto output = exec1->computeAggregates();

    std::vector<LogicalType> outTypes;
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    outTypes.push_back(LogicalType::INT64());
    std::vector<std::unique_ptr<Value>> holders;
    std::vector<Value*> values;
    for (auto& t : outTypes) {
        holders.push_back(std::make_unique<Value>(Value::createDefaultValue(t.copy())));
        values.push_back(holders.back().get());
    }
    std::map<int64_t, std::pair<int64_t, int64_t>> got;
    FlatTupleIterator it(*output, values);
    while (it.hasNextFlatTuple()) {
        it.getNextFlatTuple();
        got[values[0]->getValue<int64_t>()] = {values[1]->getValue<int64_t>(),
            values[2]->getValue<int64_t>()};
    }

    ASSERT_EQ(got.size(), static_cast<size_t>(numGroups));
    ASSERT_TRUE(got == expected)
        << "Merged (two-executor) aggregation under spilling differs from brute-force reference";
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

// Run a query and return its rows as a sorted list of strings (order-independent multiset compare).
static std::vector<std::string> collectSortedRows(main::Connection* conn,
    const std::string& query) {
    auto result = conn->query(query);
    EXPECT_TRUE(result->isSuccess()) << result->getErrorMessage();
    std::vector<std::string> rows;
    while (result->hasNext()) {
        rows.push_back(result->getNext()->toString());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

// Run a query and return its rows in result order (order-preserving; for ORDER BY comparisons).
static std::vector<std::string> collectOrderedRows(main::Connection* conn,
    const std::string& query) {
    auto result = conn->query(query);
    EXPECT_TRUE(result->isSuccess()) << result->getErrorMessage();
    std::vector<std::string> rows;
    while (result->hasNext()) {
        rows.push_back(result->getNext()->toString());
    }
    return rows;
}

// Differential correctness of the live out-of-core (Grace) HASH_JOIN operator: the same query must
// produce the same rows with `spill_hash_join` off (in-memory) and on (partitioned). Uses
// many-to-many self-joins so the probe side is flattened and the join is Grace-eligible, and asserts
// the Grace path actually activated so the comparison is not vacuous.
TEST_F(BufferManagerTest, GraceHashJoinDifferential) {
    using kuzu::processor::getGraceHashJoinActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    const std::vector<std::string> queries = {
        "MATCH (a:person), (b:person) WHERE a.gender = b.gender RETURN a.fName, b.fName",
        "MATCH (a:person), (b:person) WHERE a.age = b.age RETURN a.ID, b.ID, b.fName",
        "MATCH (a:person), (b:person) WHERE a.isStudent = b.isStudent RETURN a.ID, b.age",
    };
    const auto before = getGraceHashJoinActivationCount();
    for (const auto& q : queries) {
        ASSERT_TRUE(conn->query("CALL spill_hash_join=false;")->isSuccess());
        const auto inMemory = collectSortedRows(conn.get(), q);
        ASSERT_TRUE(conn->query("CALL spill_hash_join=true;")->isSuccess());
        const auto grace = collectSortedRows(conn.get(), q);
        ASSERT_EQ(inMemory, grace) << "Grace vs in-memory result mismatch for: " << q;
    }
    ASSERT_GT(getGraceHashJoinActivationCount(), before)
        << "no query activated the Grace hash-join path; the differential check is vacuous";
}

// Same differential check, but with a tiny per-operator budget that forces the partitions to spill
// to disk and reload (the real out-of-core path), over enough generated rows to exceed the budget.
TEST_F(BufferManagerTest, GraceHashJoinSpillDifferential) {
    using kuzu::processor::getGraceHashJoinActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE bench(id INT64, k INT64, v STRING, PRIMARY KEY(id));")
            ->isSuccess());
    // 2000 rows, each key value shared by exactly two rows -> a many-to-many self-join.
    ASSERT_TRUE(conn->query("UNWIND range(0, 1999) AS i CREATE (:bench {id: i, k: i % 1000, v: "
                            "cast(i AS STRING)});")
                    ->isSuccess());
    const std::string q = "MATCH (a:bench), (b:bench) WHERE a.k = b.k RETURN a.id, b.v";

    ASSERT_TRUE(conn->query("CALL spill_hash_join=false;")->isSuccess());
    const auto inMemory = collectSortedRows(conn.get(), q);
    ASSERT_FALSE(inMemory.empty());

    // 4 KiB budget << build side (2000 rows) -> partitions spill during append and reload on probe.
    const auto before = getGraceHashJoinActivationCount();
    ASSERT_TRUE(conn->query("CALL spill_hash_join=true;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_hash_join_budget=4096;")->isSuccess());
    const auto grace = collectSortedRows(conn.get(), q);

    ASSERT_GT(getGraceHashJoinActivationCount(), before) << "Grace path did not activate";
    ASSERT_EQ(inMemory, grace) << "Grace (spilling) vs in-memory result mismatch";
}

// Differential correctness of the live out-of-core (spilling) hash-aggregation operator: the same
// GROUP BY must produce the same rows with `spill_aggregate` off (in-memory) and on (partitioned),
// across a range of aggregates including a stateful LIST aggregate (collect). Asserts the spilling
// path actually activated so the comparison is not vacuous.
TEST_F(BufferManagerTest, SpillAggregateDifferential) {
    using kuzu::processor::getSpillAggregateActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    const std::vector<std::string> queries = {
        "MATCH (p:person) RETURN p.gender, count(*)",
        "MATCH (p:person) RETURN p.gender, count(*), sum(p.age), min(p.age), max(p.age)",
        "MATCH (p:person) RETURN p.isStudent, count(*), avg(p.age)",
        "MATCH (p:person) RETURN p.gender, collect(p.age)", // stateful LIST aggregate
        "MATCH (p:person) RETURN p.age, count(*), min(p.fName)",
    };
    const auto before = getSpillAggregateActivationCount();
    for (const auto& q : queries) {
        ASSERT_TRUE(conn->query("CALL spill_aggregate=false;")->isSuccess());
        const auto inMemory = collectSortedRows(conn.get(), q);
        ASSERT_TRUE(conn->query("CALL spill_aggregate=true;")->isSuccess());
        const auto grace = collectSortedRows(conn.get(), q);
        ASSERT_EQ(inMemory, grace) << "spill_aggregate on vs off mismatch for: " << q;
    }
    ASSERT_GT(getSpillAggregateActivationCount(), before)
        << "no query activated the spilling aggregation path; the differential check is vacuous";
}

// Same differential check, but with a tiny per-operator budget that forces the raw input rows to
// spill to disk and reload during append, over enough generated rows/groups to exceed the budget.
TEST_F(BufferManagerTest, SpillAggregateSpillDifferential) {
    using kuzu::processor::getSpillAggregateActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE bench(id INT64, k INT64, v STRING, PRIMARY KEY(id));")
            ->isSuccess());
    // 5000 rows across 50 groups (k = i % 50), so each group has 100 rows.
    ASSERT_TRUE(conn->query("UNWIND range(0, 4999) AS i CREATE (:bench {id: i, k: i % 50, v: "
                            "cast(i % 7 AS STRING)});")
                    ->isSuccess());
    const std::string q =
        "MATCH (b:bench) RETURN b.k, count(*), sum(b.id), min(b.v), collect(b.v)";

    ASSERT_TRUE(conn->query("CALL spill_aggregate=false;")->isSuccess());
    const auto inMemory = collectSortedRows(conn.get(), q);
    ASSERT_FALSE(inMemory.empty());

    // 4 KiB budget << 5000 rows -> partitions spill during append and reload on finalize.
    const auto before = getSpillAggregateActivationCount();
    ASSERT_TRUE(conn->query("CALL spill_aggregate=true;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_aggregate_budget=4096;")->isSuccess());
    const auto grace = collectSortedRows(conn.get(), q);

    ASSERT_GT(getSpillAggregateActivationCount(), before) << "spilling aggregation did not activate";
    ASSERT_EQ(inMemory, grace) << "spilling vs in-memory aggregation result mismatch";
}

// Multi-threaded out-of-core aggregation: with threads > 1, each build thread scatters into its own
// executor and they are merged at the finalize barrier. The result must match the single-threaded
// in-memory reference. (A passing run does not prove race-freedom — the design is race-free by
// construction — but it exercises the per-thread-executor + merge path end to end.)
TEST_F(BufferManagerTest, SpillAggregateMultiThreadDifferential) {
    using kuzu::processor::getSpillAggregateActivationCount;
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE bench(id INT64, k INT64, PRIMARY KEY(id));")->isSuccess());
    ASSERT_TRUE(
        conn->query("UNWIND range(0, 9999) AS i CREATE (:bench {id: i, k: i % 37});")->isSuccess());
    const std::string q = "MATCH (b:bench) RETURN b.k, count(*), sum(b.id), min(b.id)";

    // Baseline: single-threaded, in-memory.
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_aggregate=false;")->isSuccess());
    const auto baseline = collectSortedRows(conn.get(), q);
    ASSERT_FALSE(baseline.empty());

    // Multi-threaded, spilling under a tiny per-operator budget (shared across threads).
    const auto before = getSpillAggregateActivationCount();
    ASSERT_TRUE(conn->query("CALL threads=4;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_aggregate=true;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_aggregate_budget=8192;")->isSuccess());
    const auto grace = collectSortedRows(conn.get(), q);

    ASSERT_GT(getSpillAggregateActivationCount(), before)
        << "multi-threaded spilling aggregation did not activate";
    ASSERT_EQ(baseline, grace)
        << "multi-threaded spilling vs single-threaded in-memory aggregation mismatch";
}

// Verifies the spill defaults: both spill_aggregate and spill_hash_join are ON by default, so an
// eligible GROUP BY and an eligible INNER equi-join each activate the out-of-core path with no CALL
// setting.
TEST_F(BufferManagerTest, SpillDefaults) {
    using kuzu::processor::getGraceHashJoinActivationCount;
    using kuzu::processor::getSpillAggregateActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());

    const auto aggBefore = getSpillAggregateActivationCount();
    ASSERT_TRUE(conn->query("MATCH (p:person) RETURN p.gender, count(*)")->isSuccess());
    ASSERT_GT(getSpillAggregateActivationCount(), aggBefore)
        << "spill_aggregate should be ON by default";

    const auto joinBefore = getGraceHashJoinActivationCount();
    ASSERT_TRUE(conn->query("MATCH (a:person), (b:person) WHERE a.gender = b.gender "
                            "RETURN a.fName, b.fName")
                    ->isSuccess());
    ASSERT_GT(getGraceHashJoinActivationCount(), joinBefore)
        << "spill_hash_join should be ON by default";
}

// Differential correctness of the Grace join for RETURN-node / multi-column shapes (which activate
// grace by default): each query must return the same rows with spill_hash_join off (in-memory) and on
// (partitioned). Guards against the default-on path being wrong for common `RETURN *`-style joins.
TEST_F(BufferManagerTest, GraceHashJoinReturnNodeDifferential) {
    using kuzu::processor::getGraceHashJoinActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    const std::vector<std::string> queries = {
        "MATCH (a:person),(b:person) WHERE a.ID=b.ID RETURN a, b",
        "MATCH (a:person),(b:person) WHERE a.age=b.age RETURN a, b.fName",
        "MATCH (a:person),(b:person) WHERE a.gender=b.gender RETURN a.fName, a.age, b.fName, b.age",
        "MATCH (a:person),(b:person) WHERE a.gender=b.gender RETURN a, b",
    };
    const auto before = getGraceHashJoinActivationCount();
    for (const auto& q : queries) {
        ASSERT_TRUE(conn->query("CALL spill_hash_join=false;")->isSuccess());
        const auto inMemory = collectSortedRows(conn.get(), q);
        ASSERT_TRUE(conn->query("CALL spill_hash_join=true;")->isSuccess());
        const auto grace = collectSortedRows(conn.get(), q);
        ASSERT_EQ(inMemory, grace) << "Grace vs in-memory mismatch for: " << q;
    }
    // At least the all-scalar multi-column query stays Grace-eligible; the nested/NODE ones fall back.
    ASSERT_GT(getGraceHashJoinActivationCount(), before)
        << "no RETURN query activated the Grace path; the check is vacuous";
}

// Audits the spilling aggregation for nested (LIST) group keys / aggregate inputs / results, which
// the default-on path would otherwise run untested. Each GROUP BY must return the same rows with
// spill_aggregate off and on. (Whichever the operator deems ineligible simply falls back; either way
// the result must be correct.)
TEST_F(BufferManagerTest, SpillAggregateNestedDifferential) {
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    const std::vector<std::string> queries = {
        "MATCH (p:person) RETURN p.gender, collect(p.usedNames)", // nested (LIST) aggregate input
        "MATCH (p:person) RETURN p.workedHours, count(*)",        // nested (LIST) group key
        "MATCH (p:person) RETURN p.gender, count(p.workedHours)", // count over a LIST column
        "MATCH (o:organisation) RETURN o.state, count(*)", // nested (STRUCT-with-LIST) group key
    };
    for (const auto& q : queries) {
        ASSERT_TRUE(conn->query("CALL spill_aggregate=false;")->isSuccess());
        const auto inMemory = collectSortedRows(conn.get(), q);
        ASSERT_TRUE(conn->query("CALL spill_aggregate=true;")->isSuccess());
        const auto grace = collectSortedRows(conn.get(), q);
        ASSERT_EQ(inMemory, grace) << "spill_aggregate on vs off mismatch for: " << q;
    }
}

// Audits the Grace join with NULL join keys: an equi-join must never match NULL = NULL. We verify
// the Grace result against the *true* answer (computed without a join), which is the strongest check.
// This NULL-keyed self-join is also what surfaced a pre-existing in-memory hash-join bug: the build
// side undercounted (returning 10 of the 20 matching rows per key) because ValueVector::discardNull
// mishandled an already-filtered selection; that is now fixed at the root (see the null_build_key
// e2e test and docs/resource-limits-and-spilling.md), so in-memory and Grace now both return 20.
TEST_F(BufferManagerTest, GraceHashJoinNullKeyCorrectness) {
    using kuzu::processor::getGraceHashJoinActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    ASSERT_TRUE(conn->query("CALL spill_hash_join=true;")->isSuccess());
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE nk(id INT64, k INT64, PRIMARY KEY(id));")->isSuccess());
    // Half the rows (even id) have a NULL key; odd id have k = id % 10 in {1,3,5,7,9}.
    ASSERT_TRUE(conn->query("UNWIND range(0, 199) AS i CREATE (:nk {id: i, k: CASE WHEN i % 2 = 0 "
                            "THEN NULL ELSE i % 10 END});")
                    ->isSuccess());
    auto count1 = [&](const std::string& cq) {
        return std::stoll(conn->query(cq)->getNext()->toString());
    };
    // True per-key row count via a scan (no join): 20 rows have k = 1.
    const auto k1rows = count1("MATCH (a:nk) WHERE a.k = 1 RETURN count(*)");
    ASSERT_EQ(k1rows, 20);
    // a with id 101 has k = 1, so it must join exactly the k1rows rows with k = 1 -- no NULL matches.
    const auto before = getGraceHashJoinActivationCount();
    const auto a101matches =
        count1("MATCH (a:nk), (b:nk) WHERE a.k = b.k AND a.id = 101 RETURN count(*)");
    ASSERT_GT(getGraceHashJoinActivationCount(), before) << "Grace path did not activate";
    ASSERT_EQ(a101matches, k1rows) << "Grace join with NULL keys does not match the true answer";
    // Full self-join = sum over the 5 non-null keys of 20*20 = 2000; no NULL=NULL matches.
    const auto full = count1("MATCH (a:nk), (b:nk) WHERE a.k = b.k RETURN count(*)");
    ASSERT_EQ(full, 5 * 20 * 20);
}

// Audits the spilling aggregation with NULL group keys: all NULL-keyed rows must fold into a single
// NULL group. spill_aggregate off vs on must agree.
TEST_F(BufferManagerTest, SpillAggregateNullKeyDifferential) {
    using kuzu::processor::getSpillAggregateActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE nkg(id INT64, k INT64, PRIMARY KEY(id));")->isSuccess());
    ASSERT_TRUE(conn->query("UNWIND range(0, 499) AS i CREATE (:nkg {id: i, k: CASE WHEN i % 3 = 0 "
                            "THEN NULL ELSE i % 8 END});")
                    ->isSuccess());
    const std::string q = "MATCH (n:nkg) RETURN n.k, count(*), sum(n.id)";

    const auto before = getSpillAggregateActivationCount();
    ASSERT_TRUE(conn->query("CALL spill_aggregate=false;")->isSuccess());
    const auto inMemory = collectSortedRows(conn.get(), q);
    ASSERT_TRUE(conn->query("CALL spill_aggregate=true;")->isSuccess());
    const auto grace = collectSortedRows(conn.get(), q);
    ASSERT_GT(getSpillAggregateActivationCount(), before) << "spilling aggregation did not activate";
    ASSERT_EQ(inMemory, grace) << "spill_aggregate on vs off mismatch with NULL group keys";
}

// ---------------------------------------------------------------------------------------------
// External merge sort (out-of-core ORDER BY) executor.
// ---------------------------------------------------------------------------------------------

namespace {
// A row of three nullable INT64 columns: c0, c1 are the sort keys (c0 ASC, c1 DESC), c2 is a
// payload-only column. std::nullopt represents a NULL.
struct EmsRow {
    std::optional<int64_t> c0, c1, c2;
};

// Order of a single INT64 key column matching OrderByKeyEncoder's semantics: ASC sorts NULLs last,
// DESC sorts NULLs first; among non-nulls, ASC is increasing and DESC is decreasing. Returns <0, 0,
// >0.
int cmpKeyCol(const std::optional<int64_t>& x, const std::optional<int64_t>& y, bool asc) {
    if (!x.has_value() || !y.has_value()) {
        if (!x.has_value() && !y.has_value()) {
            return 0;
        }
        // The NULL side sorts last for ASC, first for DESC.
        const bool xNull = !x.has_value();
        return (xNull == asc) ? 1 : -1;
    }
    const int64_t d = (*x < *y) ? -1 : (*x > *y ? 1 : 0);
    return asc ? d : -d;
}

// Key order used to check sortedness of the output: c0 ASC then c1 DESC.
int cmpKey(const EmsRow& x, const EmsRow& y) {
    if (const int d0 = cmpKeyCol(x.c0, y.c0, true); d0 != 0) {
        return d0;
    }
    return cmpKeyCol(x.c1, y.c1, false);
}

// A total order (keys + payload c2) used only to compare the input/output as multisets.
bool fullLess(const EmsRow& x, const EmsRow& y) {
    if (const int d = cmpKey(x, y); d != 0) {
        return d < 0;
    }
    return cmpKeyCol(x.c2, y.c2, true) < 0;
}

void setCol(ValueVector& vec, uint32_t pos, const std::optional<int64_t>& v) {
    if (v.has_value()) {
        vec.setNull(pos, false);
        vec.setValue<int64_t>(pos, *v);
    } else {
        vec.setNull(pos, true);
    }
}

std::optional<int64_t> getCol(ValueVector& vec, uint32_t pos) {
    if (vec.isNull(pos)) {
        return std::nullopt;
    }
    return vec.getValue<int64_t>(pos);
}
} // namespace

// Differential-style correctness test for the out-of-core sort executor. It sorts many rows with a
// tiny budget that forces dozens of spilled runs, then asserts (a) the output is sorted by the key
// order (c0 ASC, c1 DESC, matching the encoder's NULL semantics) and (b) the output is a multiset
// permutation of the input (no rows lost, duplicated, or corrupted -- including NULLs and the
// payload-only column). Multi-column keys, ASC+DESC, NULL keys, and payload round-trip are all
// exercised.
TEST_F(BufferManagerTest, ExternalMergeSortDifferential) {
    using kuzu::processor::ExternalMergeSort;
    auto* mm = getMemoryManager(*database);
    auto* fs = getFileSystem(*database);

    // Build the OrderByDataInfo. The executor only reads keyTypes/payloadTypes/isAscOrder; the
    // positions and payload schema are supplied for completeness.
    std::vector<LogicalType> keyTypes;
    keyTypes.push_back(LogicalType::INT64());
    keyTypes.push_back(LogicalType::INT64());
    std::vector<LogicalType> payloadTypes;
    payloadTypes.push_back(LogicalType::INT64());
    payloadTypes.push_back(LogicalType::INT64());
    payloadTypes.push_back(LogicalType::INT64());
    processor::OrderByDataInfo info(
        std::vector<processor::DataPos>{processor::DataPos(0, 0), processor::DataPos(0, 1)},
        std::vector<processor::DataPos>{processor::DataPos(0, 0), processor::DataPos(0, 1),
            processor::DataPos(0, 2)},
        LogicalType::copy(keyTypes),
        LogicalType::copy(payloadTypes), std::vector<bool>{true, false},
        processor::FactorizedTableUtils::createFlatTableSchema(LogicalType::copy(payloadTypes)),
        std::vector<uint32_t>{0, 1});

    // Generate random rows with frequent key ties (small value domain) and ~12% NULLs everywhere.
    std::mt19937 rng(0xC0FFEE);
    auto randCol = [&]() -> std::optional<int64_t> {
        if (rng() % 100 < 12) {
            return std::nullopt;
        }
        return static_cast<int64_t>(rng() % 40);
    };
    const uint32_t numRows = 3000;
    std::vector<EmsRow> input;
    input.reserve(numRows);
    for (auto i = 0u; i < numRows; i++) {
        input.push_back(EmsRow{randCol(), randCol(), randCol()});
    }

    const auto spillPath =
        (std::filesystem::temp_directory_path() / "kuzu_external_merge_sort.spill").string();
    // 4 KiB budget -> dozens of runs must spill.
    ExternalMergeSort sorter(info, mm, fs, spillPath, 4096);

    // Append in batches to exercise multiple append() calls and unflat inputs.
    const uint32_t batchSize = 512;
    for (auto start = 0u; start < numRows; start += batchSize) {
        const auto n = std::min(batchSize, numRows - start);
        auto state = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
        state->initOriginalAndSelectedSize(n);
        ValueVector v0(LogicalType::INT64(), mm), v1(LogicalType::INT64(), mm),
            v2(LogicalType::INT64(), mm);
        v0.state = state;
        v1.state = state;
        v2.state = state;
        for (auto i = 0u; i < n; i++) {
            setCol(v0, i, input[start + i].c0);
            setCol(v1, i, input[start + i].c1);
            setCol(v2, i, input[start + i].c2);
        }
        std::vector<ValueVector*> keyVectors{&v0, &v1};
        std::vector<ValueVector*> payloadVectors{&v0, &v1, &v2};
        sorter.append(keyVectors, payloadVectors);
    }
    sorter.finalize();
    ASSERT_EQ(sorter.getNumTuples(), numRows);
    ASSERT_GT(sorter.getNumRuns(), 1u) << "budget did not force spilling into multiple runs";

    // Scan the sorted output back out.
    auto outState = std::make_shared<DataChunkState>(DEFAULT_VECTOR_CAPACITY);
    ValueVector o0(LogicalType::INT64(), mm), o1(LogicalType::INT64(), mm),
        o2(LogicalType::INT64(), mm);
    o0.state = outState;
    o1.state = outState;
    o2.state = outState;
    std::vector<ValueVector*> outVectors{&o0, &o1, &o2};
    std::vector<EmsRow> output;
    output.reserve(numRows);
    uint64_t produced = 0;
    while ((produced = sorter.scanNext(outVectors)) > 0) {
        for (auto i = 0u; i < produced; i++) {
            output.push_back(EmsRow{getCol(o0, i), getCol(o1, i), getCol(o2, i)});
        }
    }

    // (a) Completeness: same multiset of rows in and out.
    ASSERT_EQ(output.size(), input.size());
    auto sortedIn = input;
    auto sortedOut = output;
    std::sort(sortedIn.begin(), sortedIn.end(), fullLess);
    std::sort(sortedOut.begin(), sortedOut.end(), fullLess);
    for (auto i = 0u; i < sortedIn.size(); i++) {
        EXPECT_EQ(sortedIn[i].c0, sortedOut[i].c0) << "row " << i << " c0";
        EXPECT_EQ(sortedIn[i].c1, sortedOut[i].c1) << "row " << i << " c1";
        EXPECT_EQ(sortedIn[i].c2, sortedOut[i].c2) << "row " << i << " c2";
    }

    // (b) Sortedness: the output must be non-decreasing under the key order (c0 ASC, c1 DESC).
    for (auto i = 1u; i < output.size(); i++) {
        ASSERT_LE(cmpKey(output[i - 1], output[i]), 0)
            << "output not sorted at position " << i;
    }
}

// Differential correctness of the live out-of-core ORDER BY operator: the same query must produce the
// same ordered rows with spill_order_by off (in-memory) and on (external merge sort). Every query
// uses a total order (a unique n.id tiebreaker) so tied rows cannot legitimately differ between the
// two paths, and a tiny budget forces the external path to spill many runs. Asserts the external path
// actually activated so the comparison is not vacuous.
TEST_F(BufferManagerTest, SpillOrderByDifferential) {
    using kuzu::processor::getExternalMergeSortActivationCount;
    ASSERT_TRUE(conn->query("CALL threads=1;")->isSuccess());
    // `us` is a unique long string with a shared 12-char prefix ("commonprefix"), so ordering by it
    // ties on the encoded 12-byte prefix and must be resolved against the full payload string.
    ASSERT_TRUE(conn->query("CREATE NODE TABLE ob(id INT64, a INT64, b DOUBLE, s STRING, us STRING, "
                            "PRIMARY KEY(id));")
                    ->isSuccess());
    ASSERT_TRUE(conn->query("UNWIND range(0, 4999) AS i CREATE (:ob {id: i, a: (i * 7) % 50, "
                            "b: (i % 13) * 1.5, s: 'row' + cast(i % 20 AS STRING), "
                            "us: 'commonprefix' + cast(i AS STRING)});")
                    ->isSuccess());
    const std::vector<std::string> queries = {
        // Fixed-width keys, INT64 + STRING payload (scalar string payload round-trip).
        "MATCH (n:ob) RETURN n.a, n.id, n.s ORDER BY n.a ASC, n.id DESC",
        // DOUBLE key (descending) with a unique tiebreaker.
        "MATCH (n:ob) RETURN n.id, n.b ORDER BY n.b DESC, n.id ASC",
        // Single unique key.
        "MATCH (n:ob) RETURN n.id, n.a ORDER BY n.id DESC",
        // Unique long STRING key (ASC/DESC): every value shares the 12-char encoded prefix, so the
        // full-string tie-break decides the order.
        "MATCH (n:ob) RETURN n.id, n.us ORDER BY n.us ASC",
        "MATCH (n:ob) RETURN n.id, n.us ORDER BY n.us DESC",
        // STRING as a secondary key after a tie-heavy INT64 key.
        "MATCH (n:ob) RETURN n.a, n.us, n.id ORDER BY n.a ASC, n.us DESC",
    };
    const auto before = getExternalMergeSortActivationCount();
    for (const auto& q : queries) {
        ASSERT_TRUE(conn->query("CALL spill_order_by=false;")->isSuccess());
        const auto inMemory = collectOrderedRows(conn.get(), q);
        ASSERT_TRUE(conn->query("CALL spill_order_by=true;")->isSuccess());
        ASSERT_TRUE(conn->query("CALL spill_order_by_budget=4096;")->isSuccess());
        const auto external = collectOrderedRows(conn.get(), q);
        ASSERT_EQ(inMemory, external) << "external merge sort vs in-memory mismatch for: " << q;
    }
    ASSERT_GT(getExternalMergeSortActivationCount(), before)
        << "external merge sort path did not activate";
}

} // namespace testing
} // namespace kuzu
