#include "processor/operator/hash_join/hash_join_build.h"

#include <atomic>
#include <filesystem>

#include "binder/expression/expression_util.h"
#include "main/client_context.h"
#include "processor/execution_context.h"
#include "processor/operator/hash_join/grace_hash_join_executor.h"
#include "storage/buffer_manager/buffer_manager.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;
using namespace kuzu::storage;

namespace kuzu {
namespace processor {

// Unique suffix for the per-operator spill files, so concurrent queries never collide.
static std::atomic<uint64_t> graceSpillFileCounter{0};

// Test-only activation counter (see getGraceHashJoinActivationCount).
static std::atomic<uint64_t> graceActivationCount{0};
// Test-only: activations of specifically the multi-chunk (factorized, flat-streaming output) Grace
// path -- a subset of graceActivationCount. Lets the differential test assert that shape actually ran.
static std::atomic<uint64_t> graceMultiChunkActivationCount{0};

uint64_t getGraceHashJoinActivationCount() {
    return graceActivationCount.load();
}

uint64_t getGraceHashJoinMultiChunkActivationCount() {
    return graceMultiChunkActivationCount.load();
}

static std::unique_ptr<GraceHashJoinExecutor> makeGraceExecutor(ExecutionContext* context,
    const GraceHashJoinInfo& info, uint32_t opId) {
    auto* cc = context->clientContext;
    auto* mm = cc->getMemoryManager();
    auto* vfs = cc->getVFSUnsafe();
    // Budget: the explicit spill_hash_join_budget if set, else the per-query memory limit if set,
    // else the whole buffer pool (spill only near the hard ceiling).
    const auto* config = cc->getClientConfig();
    const auto budget = config->spillHashJoinBudget > 0 ? config->spillHashJoinBudget :
                        config->queryMemoryLimit > 0    ? config->queryMemoryLimit :
                                                          mm->getBufferManager()->getMemoryLimit();
    const auto token = graceSpillFileCounter.fetch_add(1);
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto stem = "kuzu_grace_" + std::to_string(opId) + "_" + std::to_string(token);
    auto buildPath = (tempDir / (stem + "_build.spill")).string();
    auto probePath = (tempDir / (stem + "_probe.spill")).string();
    std::vector<LogicalType> keyTypes, buildPayloadTypes, probeNonKeyTypes;
    for (auto& t : info.keyTypes) {
        keyTypes.push_back(t.copy());
    }
    for (auto& t : info.buildPayloadTypes) {
        buildPayloadTypes.push_back(t.copy());
    }
    for (auto& t : info.probeNonKeyTypes) {
        probeNonKeyTypes.push_back(t.copy());
    }
    constexpr idx_t logNumPartitions = 4; // 16 partitions
    return std::make_unique<GraceHashJoinExecutor>(mm, vfs, std::move(buildPath),
        std::move(probePath), std::move(keyTypes), std::move(buildPayloadTypes),
        std::move(probeNonKeyTypes), logNumPartitions, budget);
}

HashJoinSharedState::HashJoinSharedState(std::unique_ptr<JoinHashTable> hashTable)
    : hashTable{std::move(hashTable)} {}

HashJoinSharedState::~HashJoinSharedState() = default;

void HashJoinSharedState::setGraceExecutor(std::unique_ptr<GraceHashJoinExecutor> executor) {
    graceExecutor = std::move(executor);
}

std::string HashJoinBuildPrintInfo::toString() const {
    std::string result = "Keys: ";
    result += binder::ExpressionUtil::toString(keys);
    if (!payloads.empty()) {
        result += ", Payloads: ";
        result += binder::ExpressionUtil::toString(payloads);
    }
    return result;
}

void HashJoinSharedState::mergeLocalHashTable(JoinHashTable& localHashTable) {
    std::unique_lock lck(mtx);
    hashTable->merge(localHashTable);
}

void HashJoinBuild::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) {
    std::vector<LogicalType> keyTypes;
    for (auto i = 0u; i < info.keysPos.size(); ++i) {
        auto vector = resultSet->getValueVector(info.keysPos[i]).get();
        keyTypes.push_back(vector->dataType.copy());
        if (info.fStateTypes[i] == common::FStateType::UNFLAT) {
            setKeyState(vector->state.get());
        }
        keyVectors.push_back(vector);
    }
    if (keyState == nullptr) {
        setKeyState(keyVectors[0]->state.get());
    }
    for (auto& pos : info.payloadsPos) {
        payloadVectors.push_back(resultSet->getValueVector(pos).get());
    }
    // Decide the out-of-core (Grace) path. Single-threaded only (the probe side has no cross-thread
    // barrier), gated by `spill_hash_join`, and only for shapes the mapper marked eligible. Otherwise
    // build the in-memory hash table exactly as before.
    auto* cc = context->clientContext;
    if (cc->getClientConfig()->spillHashJoin && sharedState->getGraceInfo().eligible &&
        cc->getClientConfig()->numThreads == 1) {
        if (sharedState->getGraceExecutor() == nullptr) {
            sharedState->setGraceExecutor(makeGraceExecutor(context, sharedState->getGraceInfo(),
                id));
        }
        sharedState->setGraceActive();
        graceActivationCount.fetch_add(1);
        if (sharedState->getGraceInfo().multiChunkOutput) {
            graceMultiChunkActivationCount.fetch_add(1);
        }
    } else {
        hashTable = std::make_unique<JoinHashTable>(*cc->getMemoryManager(), std::move(keyTypes),
            info.tableSchema.copy());
    }
}

void HashJoinBuild::setKeyState(common::DataChunkState* state) {
    if (keyState == nullptr) {
        keyState = state;
    } else {
        KU_ASSERT(keyState == state); // two pointers should be pointing to the same state
    }
}

void HashJoinBuild::finalizeInternal(ExecutionContext* /*context*/) {
    if (sharedState->isGraceActive()) {
        // The Grace path partitions on append; there is no global hash table to finalize here.
        return;
    }
    auto numTuples = sharedState->getHashTable()->getNumEntries();
    sharedState->getHashTable()->allocateHashSlots(numTuples);
    sharedState->getHashTable()->buildHashSlots();
}

void HashJoinBuild::executeInternal(ExecutionContext* context) {
    if (sharedState->isGraceActive()) {
        // Out-of-core path: scatter every build row into the shared executor's partitions (which
        // spill under the memory budget). No local hash table, no merge.
        auto* exec = sharedState->getGraceExecutor();
        while (children[0]->getNextTuple(context)) {
            uint64_t numAppended = 0u;
            for (auto i = 0u; i < resultSet->multiplicity; ++i) {
                exec->appendBuild(keyVectors, payloadVectors);
                numAppended += keyState->getSelVector().getSelSize();
            }
            metrics->numOutputTuple.increase(numAppended);
        }
        return;
    }
    // Append thread-local tuples
    while (children[0]->getNextTuple(context)) {
        uint64_t numAppended = 0u;
        for (auto i = 0u; i < resultSet->multiplicity; ++i) {
            numAppended += appendVectors();
        }
        metrics->numOutputTuple.increase(numAppended);
    }
    // Merge with global hash table once local tuples are all appended.
    sharedState->mergeLocalHashTable(*hashTable);
}

} // namespace processor
} // namespace kuzu
