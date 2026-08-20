#pragma once
#include <cstdint>

#include "common/api.h"
namespace koredb {
namespace main {

struct Version {
public:
    /**
     * @brief Get the version of the KoreDB library.
     * @return const char* The version of the KoreDB library.
     */
    KOREDB_API static const char* getVersion();

    /**
     * @brief Get the storage version of the KoreDB library.
     * @return uint64_t The storage version of the KoreDB library.
     */
    KOREDB_API static uint64_t getStorageVersion();
};
} // namespace main
} // namespace koredb
