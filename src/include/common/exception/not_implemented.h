#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API NotImplementedException : public Exception {
public:
    explicit NotImplementedException(const std::string& msg) : Exception(msg){};
};

} // namespace common
} // namespace koredb
