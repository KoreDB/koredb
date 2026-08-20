#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API BufferManagerException : public Exception {
public:
    explicit BufferManagerException(const std::string& msg)
        : Exception("Buffer manager exception: " + msg){};
};

} // namespace common
} // namespace koredb
