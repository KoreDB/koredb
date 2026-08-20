#pragma once

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "aggregate_hash_table.h"
#include "common/cast.h"
#include "common/copy_constructors.h"
#include "common/data_chunk/data_chunk_state.h"
#include "common/in_mem_overflow_buffer.h"
#include "common/mpsc_queue.h"
#include "common/types/types.h"
#include "common/vector/value_vector.h"
#include "main/client_context.h"
#include "processor/operator/aggregate/aggregate_input.h"
#include "processor/operator/aggregate/base_aggregate.h"
#include "processor/operator/aggregate/partitioned_aggregate_executor.h"
#include "processor/operator/physical_operator.h"
#include "processor/result/factorized_table.h"
#include "processor/result/factorized_table_schema.h"

namespace koredb {
namespace processor {

// Test-only: how many times the out-of-core (spilling) aggregation path has been activated in this
// process. Lets a differential test confirm the spilling path actually ran instead of silently
// falling back to the in-memory path.
KOREDB_API uint64_t getSpillAggregateActivationCount();

// Static (plan-time) metadata that lets a hash aggregation run the out-of-core (spilling) path when
// `spill_aggregate` is on. `eligible` is the conservative shape check (see map_aggregate.cpp);
// aggInputTypes[i] is aggregate i's input type (ANY() for COUNT(*)) and aggResultTypes[i] its result
// type -- the two lists the PartitionedAggregateExecutor needs.
struct SpillAggregateInfo {
    bool eligible = false;
    std::vector<common::LogicalType> aggInputTypes;
    std::vector<common::LogicalType> aggResultTypes;

    SpillAggregateInfo() = default;
    SpillAggregateInfo(SpillAggregateInfo&&) = default;
    SpillAggregateInfo& operator=(SpillAggregateInfo&&) = default;
    SpillAggregateInfo(const SpillAggregateInfo&) = delete;
    SpillAggregateInfo& operator=(const SpillAggregateInfo&) = delete;
};

struct HashAggregateInfo {
    std::vector<DataPos> flatKeysPos;
    std::vector<DataPos> unFlatKeysPos;
    std::vector<DataPos> dependentKeysPos;
    FactorizedTableSchema tableSchema;

    HashAggregateInfo(std::vector<DataPos> flatKeysPos, std::vector<DataPos> unFlatKeysPos,
        std::vector<DataPos> dependentKeysPos, FactorizedTableSchema tableSchema);
    EXPLICIT_COPY_DEFAULT_MOVE(HashAggregateInfo);

private:
    HashAggregateInfo(const HashAggregateInfo& other);
};

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor): This is a final class.
class HashAggregateSharedState final : public BaseAggregateSharedState,
                                       public AggregatePartitioningData {

public:
    explicit HashAggregateSharedState(main::ClientContext* context, HashAggregateInfo hashAggInfo,
        const std::vector<function::AggregateFunction>& aggregateFunctions,
        std::span<AggregateInfo> aggregateInfos, std::vector<common::LogicalType> keyTypes,
        std::vector<common::LogicalType> payloadTypes, SpillAggregateInfo spillInfo);

    void appendTuples(const FactorizedTable& factorizedTable, ft_col_offset_t hashOffset) override {
        auto numBytesPerTuple = factorizedTable.getTableSchema()->getNumBytesPerTuple();
        for (ft_tuple_idx_t tupleIdx = 0; tupleIdx < factorizedTable.getNumTuples(); tupleIdx++) {
            auto tuple = factorizedTable.getTuple(tupleIdx);
            auto hash = *reinterpret_cast<common::hash_t*>(tuple + hashOffset);
            auto& partition =
                globalPartitions[(hash >> shiftForPartitioning) % globalPartitions.size()];
            partition.queue->appendTuple(std::span(tuple, numBytesPerTuple));
        }
    }

    void appendDistinctTuple(size_t distinctFuncIndex, std::span<uint8_t> tuple,
        common::hash_t hash) override {
        auto& partition =
            globalPartitions[(hash >> shiftForPartitioning) % globalPartitions.size()];
        partition.distinctTableQueues[distinctFuncIndex]->appendTuple(tuple);
    }

    void appendOverflow(common::InMemOverflowBuffer&& overflowBuffer) override {
        overflow.push(std::make_unique<common::InMemOverflowBuffer>(std::move(overflowBuffer)));
    }

    void finalizePartitions();

    std::pair<uint64_t, uint64_t> getNextRangeToRead() override;

    void scan(std::span<uint8_t*> entries, std::vector<common::ValueVector*>& keyVectors,
        common::offset_t startOffset, common::offset_t numRowsToScan,
        std::vector<uint32_t>& columnIndices);

    uint64_t getNumTuples() const;

    uint64_t getCurrentOffset() const { return currentOffset; }

    void setLimitNumber(uint64_t num) { limitNumber = num; }
    uint64_t getLimitNumber() const { return limitNumber; }

    const FactorizedTableSchema* getTableSchema() const {
        return globalPartitions[0].hashTable->getTableSchema();
    }

    const HashAggregateInfo& getAggregateInfo() const { return aggInfo; }

    void assertFinalized() const;

    // --- Out-of-core (spilling) aggregation ---
    // Decide the spilling path lazily at runtime (first build call) rather than at plan-mapping time,
    // so it sees the live `spill_aggregate` / `threads` settings. Idempotent (fully serialized on
    // graceMtx); safe to call from every build thread.
    void tryActivateGrace(main::ClientContext* context);
    bool isGraceActive() const { return graceActive; }
    // Build a fresh per-thread executor (its own spill file, budget = total / numThreads). Each build
    // thread scatters into its own executor so append needs no cross-thread synchronization. Only
    // valid after tryActivateGrace has activated grace.
    std::unique_ptr<PartitionedAggregateExecutor> createLocalExecutor();
    // Hand a finished per-thread executor to the shared state for merging at finalization.
    void registerLocalExecutor(std::unique_ptr<PartitionedAggregateExecutor> exec);

protected:
    std::tuple<const FactorizedTable*, common::offset_t> getPartitionForOffset(
        common::offset_t offset) const;

    struct Partition {
        std::unique_ptr<AggregateHashTable> hashTable;
        std::mutex mtx;
        std::unique_ptr<HashTableQueue> queue;
        // The tables storing the distinct values for distinct aggregate functions all get merged in
        // the same way as the main table
        std::vector<std::unique_ptr<HashTableQueue>> distinctTableQueues;
        std::atomic<bool> finalized = false;
    };

public:
    HashAggregateInfo aggInfo;
    uint64_t limitNumber;
    storage::MemoryManager* memoryManager;
    std::vector<Partition> globalPartitions;

    // Out-of-core path (all inert unless graceActive). When active, the single build thread scatters
    // raw input rows into graceExecutor (spilling under a budget) instead of the local hash table;
    // finalizePartitions aggregates each spilled partition into graceTables; and the scan-path helpers
    // (getNumTuples / getPartitionForOffset) read from graceTables instead of globalPartitions. The
    // table schema is identical to globalPartitions', so getTableSchema and the scan are unchanged.
    // graceKeyTypes/gracePayloadTypes/spillInfo are captured at construction; tryActivateGrace records
    // the per-thread budget/vfs, each build thread creates its own executor (createLocalExecutor) and
    // registers it (registerLocalExecutor), and finalizePartitions merges them into one before
    // finalizeToTables. graceMtx serializes the decision and the executor registrations.
    std::mutex graceMtx;
    bool graceDecided = false;
    bool graceActive = false;
    bool graceFinalized = false; // finalizePartitions runs on every thread; do the merge once
    uint64_t graceBudget = 0;    // per-thread spill budget
    common::VirtualFileSystem* graceVfs = nullptr;
    std::vector<common::LogicalType> graceKeyTypes;
    std::vector<common::LogicalType> gracePayloadTypes;
    SpillAggregateInfo spillInfo;
    std::vector<std::unique_ptr<PartitionedAggregateExecutor>> graceExecutors;
    std::vector<std::unique_ptr<AggregateHashTable>> graceTables;
};

struct HashAggregateLocalState {
    std::vector<common::ValueVector*> keyVectors;
    std::vector<common::ValueVector*> dependentKeyVectors;
    common::DataChunkState* leadingState = nullptr;
    std::unique_ptr<PartitioningAggregateHashTable> aggregateHashTable;
    // Per-thread out-of-core executor (only when grace is active); moved to the shared state at the
    // end of this thread's build.
    std::unique_ptr<PartitionedAggregateExecutor> graceExecutor;

    void init(HashAggregateSharedState* sharedState, ResultSet& resultSet,
        main::ClientContext* context, std::vector<function::AggregateFunction>& aggregateFunctions,
        std::vector<common::LogicalType> types);
    uint64_t append(const std::vector<AggregateInput>& aggregateInputs,
        uint64_t multiplicity) const;
};

struct HashAggregatePrintInfo final : OPPrintInfo {
    binder::expression_vector keys;
    binder::expression_vector aggregates;
    uint64_t limitNum;

    HashAggregatePrintInfo(binder::expression_vector keys, binder::expression_vector aggregates)
        : keys{std::move(keys)}, aggregates{std::move(aggregates)}, limitNum{UINT64_MAX} {}

    std::string toString() const override;

    std::unique_ptr<OPPrintInfo> copy() const override {
        return std::unique_ptr<HashAggregatePrintInfo>(new HashAggregatePrintInfo(*this));
    }

private:
    HashAggregatePrintInfo(const HashAggregatePrintInfo& other)
        : OPPrintInfo{other}, keys{other.keys}, aggregates{other.aggregates},
          limitNum{other.limitNum} {}
};

class HashAggregate final : public BaseAggregate {
public:
    HashAggregate(std::shared_ptr<BaseAggregateSharedState> sharedState,
        std::vector<function::AggregateFunction> aggregateFunctions,
        std::vector<AggregateInfo> aggInfos, std::unique_ptr<PhysicalOperator> child, uint32_t id,
        std::unique_ptr<OPPrintInfo> printInfo)
        : BaseAggregate{std::move(sharedState), std::move(aggregateFunctions), std::move(aggInfos),
              std::move(child), id, std::move(printInfo)} {}

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;

    void executeInternal(ExecutionContext* context) override;

    std::unique_ptr<PhysicalOperator> copy() override {
        return make_unique<HashAggregate>(sharedState, copyVector(aggregateFunctions),
            copyVector(aggInfos), children[0]->copy(), id, printInfo->copy());
    }

    const HashAggregateSharedState& getSharedStateReference() const {
        return common::ku_dynamic_cast<const HashAggregateSharedState&>(*sharedState);
    }
    std::shared_ptr<HashAggregateSharedState> getSharedState() const {
        return std::reinterpret_pointer_cast<HashAggregateSharedState>(sharedState);
    }

private:
    HashAggregateLocalState localState;
};

class HashAggregateFinalize final : public Sink {
    static constexpr PhysicalOperatorType type_ = PhysicalOperatorType::AGGREGATE_FINALIZE;

public:
    HashAggregateFinalize(std::shared_ptr<HashAggregateSharedState> sharedState, physical_op_id id,
        std::unique_ptr<OPPrintInfo> printInfo)
        : Sink{type_, id, std::move(printInfo)}, sharedState{std::move(sharedState)} {}

    bool isSource() const override { return true; }

    void executeInternal(ExecutionContext* /*context*/) override {
        KU_ASSERT(sharedState->isReadyForFinalization());
        sharedState->finalizePartitions();
    }
    void finalizeInternal(ExecutionContext* /*context*/) override {
        sharedState->assertFinalized();
    }

    std::unique_ptr<PhysicalOperator> copy() override {
        return make_unique<HashAggregateFinalize>(sharedState, id, printInfo->copy());
    }

private:
    std::shared_ptr<HashAggregateSharedState> sharedState;
};

} // namespace processor
} // namespace koredb
