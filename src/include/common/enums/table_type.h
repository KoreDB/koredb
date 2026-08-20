#pragma once

#include <cstdint>
#include <string>

#include "common/api.h"

namespace koredb {
namespace common {

enum class TableType : uint8_t {
    UNKNOWN = 0,
    NODE = 1,
    REL = 2,
    FOREIGN = 5,
};

struct KOREDB_API TableTypeUtils {
    static std::string toString(TableType tableType);
};

} // namespace common
} // namespace koredb
