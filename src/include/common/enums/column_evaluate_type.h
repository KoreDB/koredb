#pragma once

#include <cstdint>

namespace koredb {
namespace common {

enum class ColumnEvaluateType : uint8_t {
    REFERENCE = 0,
    DEFAULT = 1,
    CAST = 2,
};

}
} // namespace koredb
