#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API StorageException : public Exception {
public:
    explicit StorageException(const std::string& msg) : Exception("Storage exception: " + msg){};
};

} // namespace common
} // namespace koredb
