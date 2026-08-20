#pragma once

#include <string>

#include "common/types/types.h"
#include "function/function.h"
#include "pybind_include.h"

using koredb::common::LogicalTypeID;
using koredb::function::function_set;

namespace koredb {
namespace main {
class ClientContext;
} // namespace main
} // namespace koredb

class PyUDF {

public:
    static function_set toFunctionSet(const std::string& name, const py::function& udf,
        const py::list& paramTypes, const std::string& resultType, bool defaultNull,
        bool catchExceptions, koredb::main::ClientContext* context);
};
