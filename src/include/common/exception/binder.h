#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API BinderException : public Exception {
public:
    explicit BinderException(const std::string& msg) : Exception("Binder exception: " + msg){};
};

} // namespace common
} // namespace koredb
