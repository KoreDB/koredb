#include "processor/operator/ddl/create_type.h"

#include "catalog/catalog.h"
#include "processor/execution_context.h"

using namespace koredb::catalog;
using namespace koredb::common;

namespace koredb {
namespace processor {

std::string CreateTypePrintInfo::toString() const {
    return typeName + " AS " + type;
}

void CreateType::executeInternal(ExecutionContext* context) {
    auto clientContext = context->clientContext;
    clientContext->getCatalog()->createType(clientContext->getTransaction(), name, type.copy());
    appendMessage(stringFormat("Type {}({}) has been created.", name, type.toString()),
        clientContext->getMemoryManager());
}

} // namespace processor
} // namespace koredb
