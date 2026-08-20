#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API TransactionManagerException : public Exception {
public:
    explicit TransactionManagerException(const std::string& msg) : Exception(msg){};
};

} // namespace common
} // namespace koredb
