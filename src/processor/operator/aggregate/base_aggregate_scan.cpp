#include "processor/operator/aggregate/base_aggregate_scan.h"

using namespace koredb::common;
using namespace koredb::function;

namespace koredb {
namespace processor {

void BaseAggregateScan::initLocalStateInternal(ResultSet* resultSet,
    ExecutionContext* /*context*/) {
    for (auto& dataPos : scanInfo.aggregatesPos) {
        auto valueVector = resultSet->getValueVector(dataPos);
        aggregateVectors.push_back(valueVector);
    }
}

} // namespace processor
} // namespace koredb
