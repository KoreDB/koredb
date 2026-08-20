#pragma once

#include <cstdint>

#include "common/api.h"

namespace koredb {
namespace common {

class Value;

class NestedVal {
public:
    KOREDB_API static uint32_t getChildrenSize(const Value* val);

    KOREDB_API static Value* getChildVal(const Value* val, uint32_t idx);
};

} // namespace common
} // namespace koredb
