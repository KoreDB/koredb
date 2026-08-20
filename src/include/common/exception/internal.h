#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API InternalException : public Exception {
public:
    explicit InternalException(const std::string& msg) : Exception(msg){};
};

} // namespace common
} // namespace koredb
