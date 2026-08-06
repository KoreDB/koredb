#include "processor/operator/aggregate/hash_aggregate.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>

#include "binder/expression/expression_util.h"
#include "common/assert.h"
#include "common/types/types.h"
#include "main/client_context.h"
#include "processor/execution_context.h"
#include "processor/operator/aggregate/aggregate_hash_table.h"
#include "processor/operator/aggregate/aggregate_input.h"
#include "processor/operator/aggregate/base_aggregate.h"
#include "processor/result/factorized_table_schema.h"
#include "storage/buffer_manager/buffer_manager.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;
using namespace kuzu::function;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

// Test-only activation counter (see getSpillAggregateActivationCount).
static std::atomic<uint64_t> spillAggregateActivationCount{0};

uint64_t getSpillAggregateActivationCount() {
    return spillAggregateActivationCount.load();
}

std::string HashAggregatePrintInfo::toString() const {
    std::string result = "";
    result += "Group By: ";
    result += binder::ExpressionUtil::toString(keys);
    if (!aggregates.empty()) {
        result += ", Aggregates: ";
        result += binder::ExpressionUtil::toString(aggregates);
    }
    if (limitNum != UINT64_MAX) {
        result += ", Distinct Limit: " + std::to_string(limitNum);
    }
    return result;
}

HashAggregateInfo::HashAggregateInfo(std::vector<DataPos> flatKeysPos,
    std::vector<DataPos> unFlatKeysPos, std::vector<DataPos> dependentKeysPos,
    FactorizedTableSchema tableSchema)
    : flatKeysPos{std::move(flatKeysPos)}, unFlatKeysPos{std::move(unFlatKeysPos)},
      dependentKeysPos{std::move(dependentKeysPos)}, tableSchema{std::move(tableSchema)} {}

HashAggregateInfo::HashAggregateInfo(const HashAggregateInfo& other)
    : flatKeysPos{other.flatKeysPos}, unFlatKeysPos{other.unFlatKeysPos},
      dependentKeysPos{other.dependentKeysPos}, tableSchema{other.tableSchema.copy()} {}

HashAggregateSharedState::HashAggregateSharedState(main::ClientContext* context,
    HashAggregateInfo hashAggInfo,
    const std::vector<function::AggregateFunction>& aggregateFunctions,
    std::span<AggregateInfo> aggregateInfos, std::vector<LogicalType> keyTypes,
    std::vector<LogicalType> payloadTypes, SpillAggregateInfo spillInfoParam)
    : BaseAggregateSharedState{aggregateFunctions, getNumPartitionsForParallelism(context)},
      aggInfo{std::move(hashAggInfo)}, limitNumber{common::INVALID_LIMIT},
      memoryManager{context->getMemoryManager()},
      globalPartitions{getNumPartitionsForParallelism(context)} {
    // Capture key/payload type copies and the spill metadata; the out-of-core executor is built
    // lazily in tryActivateGrace (at runtime, so it sees the live settings), not here.
    graceKeyTypes = LogicalType::copy(keyTypes);
    gracePayloadTypes = LogicalType::copy(payloadTypes);
    spillInfo = std::move(spillInfoParam);
    std::vector<LogicalType> distinctAggregateKeyTypes;
    for (auto& aggInfo : aggregateInfos) {
        distinctAggregateKeyTypes.push_back(aggInfo.distinctAggKeyType.copy());
    }

    // When copying directly into factorizedTables the table's schema's internal mayContainNulls
    // won't be updated and it's probably less work to just always check nulls
    // Skip the last column, which is the hash column and should never contain nulls
    for (size_t i = 0; i < this->aggInfo.tableSchema.getNumColumns() - 1; i++) {
        this->aggInfo.tableSchema.setMayContainsNullsToTrue(i);
    }

    auto& partition = globalPartitions[0];
    partition.queue = std::make_unique<HashTableQueue>(context->getMemoryManager(),
        this->aggInfo.tableSchema.copy());

    // Always create a hash table for the first partition. Any other partitions which are non-empty
    // when finalizing will create an empty copy of this table
    partition.hashTable = std::make_unique<AggregateHashTable>(*context->getMemoryManager(),
        std::move(keyTypes), std::move(payloadTypes), aggregateFunctions, distinctAggregateKeyTypes,
        0, this->aggInfo.tableSchema.copy());
    for (size_t functionIdx = 0; functionIdx < aggregateFunctions.size(); functionIdx++) {
        auto& function = aggregateFunctions[functionIdx];
        if (function.isFunctionDistinct()) {
            // Create table schema for distinct hash table
            auto distinctTableSchema = FactorizedTableSchema();
            // Group by key columns
            for (size_t i = 0;
                 i < this->aggInfo.flatKeysPos.size() + this->aggInfo.unFlatKeysPos.size(); i++) {
                distinctTableSchema.appendColumn(this->aggInfo.tableSchema.getColumn(i)->copy());
                distinctTableSchema.setMayContainsNullsToTrue(i);
            }
            // Distinct key column
            distinctTableSchema.appendColumn(ColumnSchema(false /*isUnFlat*/, 0 /*groupID*/,
                LogicalTypeUtils::getRowLayoutSize(
                    aggregateInfos[functionIdx].distinctAggKeyType)));
            distinctTableSchema.setMayContainsNullsToTrue(distinctTableSchema.getNumColumns() - 1);
            // Hash column
            distinctTableSchema.appendColumn(
                ColumnSchema(false /* isUnFlat */, 0 /* groupID */, sizeof(hash_t)));

            partition.distinctTableQueues.emplace_back(std::make_unique<HashTableQueue>(
                context->getMemoryManager(), std::move(distinctTableSchema)));
        } else {
            // dummy entry so that indices line up with the aggregateFunctions
            partition.distinctTableQueues.emplace_back();
        }
    }
    // Each partition is the same, so we create the list of distinct queues for the first partition
    // and copy it to the other partitions
    for (size_t i = 1; i < globalPartitions.size(); i++) {
        globalPartitions[i].queue = std::make_unique<HashTableQueue>(context->getMemoryManager(),
            this->aggInfo.tableSchema.copy());
        globalPartitions[i].distinctTableQueues.resize(partition.distinctTableQueues.size());
        std::transform(partition.distinctTableQueues.begin(), partition.distinctTableQueues.end(),
            globalPartitions[i].distinctTableQueues.begin(), [&](auto& q) {
                if (q.get() != nullptr) {
                    return q->copy();
                } else {
                    return std::unique_ptr<HashTableQueue>();
                }
            });
    }
}

void HashAggregateSharedState::tryActivateGrace(main::ClientContext* context) {
    std::unique_lock lck{graceMtx};
    if (graceDecided) {
        return;
    }
    graceDecided = true;
    // On when `spill_aggregate` is set and the shape is eligible. Multi-threaded is supported: each
    // build thread scatters into its own executor (createLocalExecutor) with no shared mutation, and
    // the executors are merged at the finalize barrier. Otherwise the grace members stay inert and the
    // in-memory path runs.
    const auto* cc = context->getClientConfig();
    if (!(cc->spillAggregate && spillInfo.eligible)) {
        return;
    }
    auto* bm = memoryManager->getBufferManager();
    const auto totalBudget = cc->spillAggregateBudget > 0 ? cc->spillAggregateBudget :
                             cc->queryMemoryLimit > 0    ? cc->queryMemoryLimit :
                                                           bm->getMemoryLimit();
    const auto nThreads = std::max<uint64_t>(1, cc->numThreads);
    graceBudget = std::max<uint64_t>(1, totalBudget / nThreads); // per-thread share of the budget
    graceVfs = context->getVFSUnsafe();
    graceActive = true;
    spillAggregateActivationCount.fetch_add(1);
}

std::unique_ptr<PartitionedAggregateExecutor> HashAggregateSharedState::createLocalExecutor() {
    static std::atomic<uint64_t> spillFileCounter{0};
    const auto token = spillFileCounter.fetch_add(1);
    auto path = (std::filesystem::temp_directory_path() /
                 ("kuzu_spill_agg_" + std::to_string(token) + ".spill"))
                    .string();
    return std::make_unique<PartitionedAggregateExecutor>(memoryManager, graceVfs, std::move(path),
        LogicalType::copy(graceKeyTypes), LogicalType::copy(gracePayloadTypes),
        copyVector(aggregateFunctions), LogicalType::copy(spillInfo.aggInputTypes),
        LogicalType::copy(spillInfo.aggResultTypes), 4 /*logNumPartitions*/, graceBudget);
}

void HashAggregateSharedState::registerLocalExecutor(
    std::unique_ptr<PartitionedAggregateExecutor> exec) {
    std::unique_lock lck{graceMtx};
    graceExecutors.push_back(std::move(exec));
}

std::pair<uint64_t, uint64_t> HashAggregateSharedState::getNextRangeToRead() {
    std::unique_lock lck{mtx};
    auto startOffset = currentOffset.load();
    auto numTuples = getNumTuples();
    if (startOffset >= numTuples) {
        return std::make_pair(startOffset, startOffset);
    }
    // FactorizedTable::lookup resets the ValueVector and writes to the beginning,
    // so we can't support scanning from multiple partitions at once
    auto [table, tableStartOffset] = getPartitionForOffset(startOffset);
    auto range = std::min(std::min(DEFAULT_VECTOR_CAPACITY, numTuples - startOffset),
        table->getNumTuples() + tableStartOffset - startOffset);
    currentOffset += range;
    return std::make_pair(startOffset, startOffset + range);
}

uint64_t HashAggregateSharedState::getNumTuples() const {
    uint64_t numTuples = 0;
    if (graceActive) {
        for (auto& table : graceTables) {
            numTuples += table->getNumEntries();
        }
        return numTuples;
    }
    for (auto& partition : globalPartitions) {
        numTuples += partition.hashTable->getNumEntries();
    }
    return numTuples;
}

void HashAggregateSharedState::finalizePartitions() {
    if (graceActive) {
        // finalizePartitions runs on every build thread; do the merge + finalize exactly once (others
        // block on the lock, then observe graceFinalized and skip). Merge the per-thread executors
        // into one (co-partitioned, so a group still lands in a single merged partition), then
        // aggregate every spilled partition into a finalized in-memory table. The scan path reads
        // these exactly as it reads globalPartitions' tables (identical schema).
        std::unique_lock lck{graceMtx};
        if (graceFinalized) {
            return;
        }
        graceFinalized = true;
        if (!graceExecutors.empty()) {
            auto& merged = graceExecutors[0];
            for (size_t i = 1; i < graceExecutors.size(); i++) {
                merged->merge(*graceExecutors[i]);
            }
            graceTables = merged->finalizeToTables();
        }
        return;
    }
    BaseAggregateSharedState::finalizePartitions(globalPartitions, [&](auto& partition) {
        if (!partition.hashTable) {
            // We always initialize the hash table in the first partition
            partition.hashTable = std::make_unique<AggregateHashTable>(
                globalPartitions[0].hashTable->createEmptyCopy());
        }
        // TODO(bmwinger): ideally these can be merged into a single function.
        // The distinct tables need to be merged first so that they exist when the other table
        // updates the agg states when it merges
        for (size_t i = 0; i < partition.distinctTableQueues.size(); i++) {
            if (partition.distinctTableQueues[i]) {
                partition.distinctTableQueues[i]->mergeInto(
                    *partition.hashTable->getDistinctHashTable(i));
            }
        }
        partition.queue->mergeInto(*partition.hashTable);
        partition.hashTable->mergeDistinctAggregateInfo();

        partition.hashTable->finalizeAggregateStates();
    });
}

std::tuple<const FactorizedTable*, offset_t> HashAggregateSharedState::getPartitionForOffset(
    offset_t offset) const {
    offset_t factorizedTableStartOffset = 0;
    size_t partitionIdx = 0;
    auto tableAt = [&](size_t idx) -> const FactorizedTable* {
        return graceActive ? graceTables[idx]->getFactorizedTable() :
                             globalPartitions[idx].hashTable->getFactorizedTable();
    };
    const auto* table = tableAt(partitionIdx);
    while (factorizedTableStartOffset + table->getNumTuples() <= offset) {
        factorizedTableStartOffset += table->getNumTuples();
        table = tableAt(++partitionIdx);
    }
    return std::make_tuple(table, factorizedTableStartOffset);
}

void HashAggregateSharedState::scan(std::span<uint8_t*> entries,
    std::vector<common::ValueVector*>& keyVectors, offset_t startOffset, offset_t numTuplesToScan,
    std::vector<uint32_t>& columnIndices) {
    auto [table, tableStartOffset] = getPartitionForOffset(startOffset);
    // Due to the way FactorizedTable::lookup works, it's necessary to read one partition
    // at a time.
    KU_ASSERT(startOffset - tableStartOffset + numTuplesToScan <= table->getNumTuples());
    for (size_t pos = 0; pos < numTuplesToScan; pos++) {
        auto posInTable = startOffset + pos - tableStartOffset;
        entries[pos] = table->getTuple(posInTable);
    }
    table->lookup(keyVectors, columnIndices, entries.data(), 0, numTuplesToScan);
    KU_ASSERT(true);
}

void HashAggregateSharedState::assertFinalized() const {
    if (graceActive) {
        return; // grace path finalizes into graceTables, not the globalPartitions queues
    }
    RUNTIME_CHECK(for (const auto& partition
                       : globalPartitions) {
        KU_ASSERT(partition.finalized);
        KU_ASSERT(partition.queue->empty());
    });
}

void HashAggregateLocalState::init(HashAggregateSharedState* sharedState, ResultSet& resultSet,
    main::ClientContext* context, std::vector<function::AggregateFunction>& aggregateFunctions,
    std::vector<common::LogicalType> distinctKeyTypes) {
    auto& info = sharedState->getAggregateInfo();
    std::vector<LogicalType> keyDataTypes;
    for (auto& pos : info.flatKeysPos) {
        auto vector = resultSet.getValueVector(pos).get();
        keyVectors.push_back(vector);
        keyDataTypes.push_back(vector->dataType.copy());
    }
    for (auto& pos : info.unFlatKeysPos) {
        auto vector = resultSet.getValueVector(pos).get();
        keyVectors.push_back(vector);
        keyDataTypes.push_back(vector->dataType.copy());
        leadingState = vector->state.get();
    }
    if (leadingState == nullptr) {
        // All vectors are flat, so any can be the leading state
        leadingState = keyVectors.front()->state.get();
    }
    std::vector<LogicalType> payloadDataTypes;
    for (auto& pos : info.dependentKeysPos) {
        auto vector = resultSet.getValueVector(pos).get();
        dependentKeyVectors.push_back(vector);
        payloadDataTypes.push_back(vector->dataType.copy());
    }

    aggregateHashTable = std::make_unique<PartitioningAggregateHashTable>(sharedState,
        *context->getMemoryManager(), std::move(keyDataTypes), std::move(payloadDataTypes),
        aggregateFunctions, std::move(distinctKeyTypes), info.tableSchema.copy());
}

uint64_t HashAggregateLocalState::append(const std::vector<AggregateInput>& aggregateInputs,
    uint64_t multiplicity) const {
    return aggregateHashTable->append(keyVectors, dependentKeyVectors, leadingState,
        aggregateInputs, multiplicity);
}

void HashAggregate::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) {
    BaseAggregate::initLocalStateInternal(resultSet, context);
    std::vector<LogicalType> distinctAggKeyTypes;
    for (auto& info : aggInfos) {
        distinctAggKeyTypes.push_back(info.distinctAggKeyType.copy());
    }
    auto* ss = common::ku_dynamic_cast<HashAggregateSharedState*>(sharedState.get());
    // Decide the out-of-core path now (runtime), so it reflects the live `spill_aggregate` setting.
    ss->tryActivateGrace(context->clientContext);
    localState.init(ss, *resultSet, context->clientContext, aggregateFunctions,
        std::move(distinctAggKeyTypes));
    if (ss->isGraceActive()) {
        localState.graceExecutor = ss->createLocalExecutor();
    }
}

void HashAggregate::executeInternal(ExecutionContext* context) {
    if (getSharedStateReference().isGraceActive()) {
        // Out-of-core path: scatter every raw input row into this thread's own executor (which spills
        // under the per-thread budget). No local hash table, no cross-thread synchronization; the
        // executor is handed off to the shared state for merging once this thread's input is drained.
        auto* exec = localState.graceExecutor.get();
        std::vector<ValueVector*> aggInputVectors; // one per function; nullptr for COUNT(*)
        aggInputVectors.reserve(aggInputs.size());
        for (auto& in : aggInputs) {
            aggInputVectors.push_back(in.aggregateVector);
        }
        while (children[0]->getNextTuple(context)) {
            exec->append(localState.keyVectors, localState.dependentKeyVectors, aggInputVectors,
                resultSet->multiplicity);
        }
        getSharedState()->registerLocalExecutor(std::move(localState.graceExecutor));
        return;
    }
    while (children[0]->getNextTuple(context)) {
        const auto numAppendedFlatTuples = localState.append(aggInputs, resultSet->multiplicity);
        metrics->numOutputTuple.increase(numAppendedFlatTuples);
        // Note: The limit count check here is only applicable to the distinct limit case.
        if (localState.aggregateHashTable->getNumEntries() >=
            getSharedStateReference().getLimitNumber()) {
            break;
        }
    }
    localState.aggregateHashTable->mergeIfFull(0 /*tuplesToAdd*/, true /*mergeAll*/);
}

} // namespace processor
} // namespace kuzu
