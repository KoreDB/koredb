#include "binder/binder.h"
#include "binder/bound_transaction_statement.h"
#include "parser/transaction_statement.h"

using namespace koredb::parser;

namespace koredb {
namespace binder {

std::unique_ptr<BoundStatement> Binder::bindTransaction(const Statement& statement) {
    auto& transactionStatement = statement.constCast<TransactionStatement>();
    return std::make_unique<BoundTransactionStatement>(transactionStatement.getTransactionAction());
}

} // namespace binder
} // namespace koredb
