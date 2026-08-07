#include "processor/operator/order_by/order_by.h"

#include <atomic>
#include <filesystem>
#include <optional>

#include "binder/expression/expression_util.h"
#include "common/types/types.h"
#include "main/client_context.h"
#include "processor/execution_context.h"
#include "processor/operator/order_by/external_merge_sort.h"
#include "storage/buffer_manager/buffer_manager.h"
#include "storage/buffer_manager/memory_manager.h"

using namespace kuzu::common;

namespace kuzu {
namespace processor {

// Unique suffix for the per-operator spill files, so concurrent queries never collide.
static std::atomic<uint64_t> externalSortSpillFileCounter{0};
// Test-only activation counter (see getExternalMergeSortActivationCount).
static std::atomic<uint64_t> externalSortActivationCount{0};

uint64_t getExternalMergeSortActivationCount() {
    return externalSortActivationCount.load();
}

// The external merge sort supports fixed-width and STRING keys (STRING prefix ties are resolved
// against the full payload string) and flat payloads read one value per tuple via getAsValue, all in
// a single input data chunk (so key and payload positions line up). Nested payloads and multi-chunk
// (factorized) inputs fall back to the in-memory sort. A nested *key* never reaches here at all -- the
// binder rejects ORDER BY on nested types engine-wide (isOrderByKeyTypeSupported), so the nested-key
// check below is purely defensive.
static bool isExternalSortEligible(const OrderByDataInfo& info) {
    for (auto& keyType : info.keyTypes) {
        if (LogicalTypeUtils::isNested(keyType)) {
            return false;
        }
    }
    for (auto& payloadType : info.payloadTypes) {
        if (LogicalTypeUtils::isNested(payloadType)) {
            return false;
        }
    }
    std::optional<data_chunk_pos_t> chunk;
    auto sameChunk = [&chunk](const std::vector<DataPos>& positions) {
        for (auto& p : positions) {
            if (!chunk.has_value()) {
                chunk = p.dataChunkPos;
            } else if (*chunk != p.dataChunkPos) {
                return false;
            }
        }
        return true;
    };
    return sameChunk(info.keysPos) && sameChunk(info.payloadsPos);
}

static std::unique_ptr<ExternalMergeSort> makeExternalSorter(ExecutionContext* context,
    const OrderByDataInfo& info, uint32_t opId) {
    auto* cc = context->clientContext;
    auto* mm = cc->getMemoryManager();
    auto* vfs = cc->getVFSUnsafe();
    // Budget: the explicit spill_order_by_budget if set, else the per-query memory limit if set, else
    // the whole buffer pool (spill only near the hard ceiling).
    const auto* config = cc->getClientConfig();
    auto budget = config->spillOrderByBudget > 0 ? config->spillOrderByBudget :
                  config->queryMemoryLimit > 0   ? config->queryMemoryLimit :
                                                   mm->getBufferManager()->getMemoryLimit();
    // Each thread runs its own generator concurrently, so split the budget evenly to keep the
    // aggregate resident footprint bounded (fall back to the whole budget if that would round to 0).
    const auto numThreads = std::max<uint64_t>(1, config->numThreads);
    if (budget / numThreads > 0) {
        budget /= numThreads;
    }
    const auto token = externalSortSpillFileCounter.fetch_add(1);
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto stem = "kuzu_ems_" + std::to_string(opId) + "_" + std::to_string(token) + ".spill";
    auto spillPath = (tempDir / stem).string();
    return std::make_unique<ExternalMergeSort>(info, mm, vfs, std::move(spillPath), budget);
}

std::string OrderByPrintInfo::toString() const {
    std::string result = "Order By: ";
    result += binder::ExpressionUtil::toString(keys);
    result += ", Expressions: ";
    result += binder::ExpressionUtil::toString(payloads);
    return result;
}

void OrderBy::initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) {
    for (auto& dataPos : info.payloadsPos) {
        payloadVectors.push_back(resultSet->getValueVector(dataPos).get());
    }
    for (auto& dataPos : info.keysPos) {
        orderByVectors.push_back(resultSet->getValueVector(dataPos).get());
    }
    // Decide the out-of-core (external merge sort) path, gated by `spill_order_by` and only for
    // eligible key/payload shapes. The decision is deterministic, so every thread agrees. Each thread
    // registers its own generator (run generation is embarrassingly parallel); the cross-thread k-way
    // merge happens later on the single scan thread (see SortSharedState::prepareExternalMerge).
    // Otherwise fall back to the in-memory sort.
    auto* cc = context->clientContext;
    if (cc->getClientConfig()->spillOrderBy && isExternalSortEligible(info)) {
        externalActive = true;
        externalGen = sharedState->addExternalGenerator(makeExternalSorter(context, info, id));
        sharedState->setExternalActive();
        externalSortActivationCount.fetch_add(1);
    } else {
        localState = SortLocalState();
        localState.init(info, *sharedState, cc->getMemoryManager());
    }
}

void OrderBy::initGlobalStateInternal(ExecutionContext* /*context*/) {
    sharedState->init(info);
}

void OrderBy::executeInternal(ExecutionContext* context) {
    if (externalActive) {
        // Append + finalize this thread's own generator (no shared mutation on the hot path).
        while (children[0]->getNextTuple(context)) {
            for (auto i = 0u; i < resultSet->multiplicity; i++) {
                externalGen->append(orderByVectors, payloadVectors);
            }
        }
        externalGen->finalize();
        return;
    }
    // Append thread-local tuples.
    while (children[0]->getNextTuple(context)) {
        for (auto i = 0u; i < resultSet->multiplicity; i++) {
            localState.append(orderByVectors, payloadVectors);
        }
    }
    localState.finalize(*sharedState);
}

} // namespace processor
} // namespace kuzu
