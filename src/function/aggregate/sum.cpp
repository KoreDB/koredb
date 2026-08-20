#include "function/aggregate/sum.h"

namespace koredb {
namespace function {

using namespace koredb::common;

function_set AggregateSumFunction::getFunctionSet() {
    function_set result;
    for (auto typeID : LogicalTypeUtils::getNumericalLogicalTypeIDs()) {
        AggregateFunctionUtils::appendSumOrAvgFuncs<SumFunction>(name, typeID, result);
    }
    return result;
}

} // namespace function
} // namespace koredb
