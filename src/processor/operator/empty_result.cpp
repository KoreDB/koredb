#include "processor/operator/empty_result.h"

namespace koredb {
namespace processor {

bool EmptyResult::getNextTuplesInternal(ExecutionContext*) {
    return false;
}

} // namespace processor
} // namespace koredb
