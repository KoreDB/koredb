#pragma once

#include <cstdint>

namespace koredb {
namespace common {

enum class SubqueryType : uint8_t {
    COUNT = 1,
    EXISTS = 2,
};

}
} // namespace koredb
