#pragma once

#include <cstdint>

namespace koredb {
namespace function {

enum class GDSDensityState : uint8_t {
    SPARSE = 0,
    DENSE = 1,
};

}
} // namespace koredb
