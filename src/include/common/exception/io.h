#pragma once

#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API IOException : public Exception {
public:
    explicit IOException(const std::string& msg) : Exception("IO exception: " + msg) {}
};

} // namespace common
} // namespace koredb
