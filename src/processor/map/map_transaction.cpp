#include "planner/operator/logical_transaction.h"
#include "processor/operator/transaction.h"
#include "processor/plan_mapper.h"

using namespace koredb::planner;

namespace koredb {
namespace processor {

std::unique_ptr<PhysicalOperator> PlanMapper::mapTransaction(
    const LogicalOperator* logicalOperator) {
    auto& logicalTransaction = logicalOperator->constCast<LogicalTransaction>();
    auto printInfo =
        std::make_unique<TransactionPrintInfo>(logicalTransaction.getTransactionAction());
    return std::make_unique<Transaction>(logicalTransaction.getTransactionAction(), getOperatorID(),
        std::move(printInfo));
}

} // namespace processor
} // namespace koredb
