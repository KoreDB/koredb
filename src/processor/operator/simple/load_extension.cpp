#include "processor/operator/simple/load_extension.h"

#include "extension/extension_manager.h"
#include "processor/execution_context.h"

using namespace koredb::common;

namespace koredb {
namespace processor {

using namespace koredb::extension;

std::string LoadExtensionPrintInfo::toString() const {
    return "Load " + extensionName;
}

void LoadExtension::executeInternal(ExecutionContext* context) {
    auto clientContext = context->clientContext;
    if (ExtensionUtils::isOfficialExtension(path) &&
        clientContext->getExtensionManager()->isStaticLinkedExtension(path)) {
        appendMessage(
            stringFormat(
                "Extension {} is already statically linked with the koredb core. No need to LOAD.",
                path),
            context->clientContext->getMemoryManager());
        return;
    }
    clientContext->getExtensionManager()->loadExtension(path, clientContext);
    appendMessage(stringFormat("Extension: {} has been loaded.", path),
        clientContext->getMemoryManager());
}

} // namespace processor
} // namespace koredb
