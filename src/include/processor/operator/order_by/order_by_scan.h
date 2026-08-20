#pragma once

#include "processor/operator/order_by/sort_state.h"
#include "processor/operator/physical_operator.h"

namespace koredb {
namespace processor {

struct OrderByScanLocalState {
    std::vector<common::ValueVector*> vectorsToRead;
    std::unique_ptr<PayloadScanner> payloadScanner;
    // Set (and payloadScanner left null) when the shared state ran the out-of-core sort.
    ExternalMergeSort* externalSorter = nullptr;
    uint64_t numTuples = 0;
    uint64_t numTuplesRead = 0;

    void init(std::vector<DataPos>& outVectorPos, SortSharedState& sharedState,
        ResultSet& resultSet);

    // Scans the next batch of sorted tuples; out-of-line because it may touch the incomplete
    // ExternalMergeSort type.
    uint64_t scan();
};

// To preserve the ordering of tuples, the orderByScan operator will only
// be executed in single-thread mode.
class OrderByScan final : public PhysicalOperator {
    static constexpr PhysicalOperatorType type_ = PhysicalOperatorType::ORDER_BY_SCAN;

public:
    OrderByScan(std::vector<DataPos> outVectorPos, std::shared_ptr<SortSharedState> sharedState,
        uint32_t id, std::unique_ptr<OPPrintInfo> printInfo)
        : PhysicalOperator{type_, id, std::move(printInfo)}, outVectorPos{std::move(outVectorPos)},
          localState{std::make_unique<OrderByScanLocalState>()},
          sharedState{std::move(sharedState)} {}

    bool isSource() const override { return true; }
    // Ordered table should be scanned in single-thread mode.
    bool isParallel() const override { return false; }

    bool getNextTuplesInternal(ExecutionContext* context) override;

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;

    std::unique_ptr<PhysicalOperator> copy() override {
        return std::make_unique<OrderByScan>(outVectorPos, sharedState, id, printInfo->copy());
    }

    double getProgress(ExecutionContext* context) const override;

private:
    std::vector<DataPos> outVectorPos;
    std::unique_ptr<OrderByScanLocalState> localState;
    std::shared_ptr<SortSharedState> sharedState;
};

} // namespace processor
} // namespace koredb
