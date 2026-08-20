#include "binder/binder.h"
#include "extension/binder_extension.h"

using namespace koredb::common;
using namespace koredb::parser;

namespace koredb {
namespace binder {

std::unique_ptr<BoundStatement> Binder::bindExtensionClause(const parser::Statement& statement) {
    for (auto& binderExtension : binderExtensions) {
        auto boundStatement = binderExtension->bind(statement);
        if (boundStatement) {
            return boundStatement;
        }
    }
    KU_UNREACHABLE;
}

} // namespace binder
} // namespace koredb
