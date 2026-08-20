#pragma once

#include "planner/operator/operator_print_info.h"

namespace koredb {
namespace processor {

struct ExtensionPrintInfo : OPPrintInfo {
    std::string extensionName;

    explicit ExtensionPrintInfo(std::string extensionName)
        : extensionName{std::move(extensionName)} {}
};

} // namespace processor
} // namespace koredb
