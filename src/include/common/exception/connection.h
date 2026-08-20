#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API ConnectionException : public Exception {
public:
    explicit ConnectionException(const std::string& msg)
        : Exception("Connection exception: " + msg){};
};

} // namespace common
} // namespace koredb
