#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
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
#include "function/hash/vector_hash_functions.h"
#include "graph_test/private_graph_test.h"
#include "gtest/gtest.h"
#include "processor/data_pos.h"
#include "processor/operator/hash_join/grace_hash_join_executor.h"
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
