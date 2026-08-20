#pragma once

#include "binder/expression/expression.h"
#include "common/types/value/value.h"
#include "main/client_context.h"

namespace koredb {
namespace evaluator {

struct ExpressionEvaluatorUtils {
    static KOREDB_API common::Value evaluateConstantExpression(
        std::shared_ptr<binder::Expression> expression, main::ClientContext* clientContext);
};

} // namespace evaluator
} // namespace koredb
