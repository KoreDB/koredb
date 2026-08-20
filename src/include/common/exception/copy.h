#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API CopyException : public Exception {
public:
    explicit CopyException(const std::string& msg) : Exception("Copy exception: " + msg){};
};

} // namespace common
} // namespace koredb
