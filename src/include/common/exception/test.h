#pragma once

#include "exception.h"

namespace koredb {
namespace common {

class TestException : public Exception {
public:
    explicit TestException(const std::string& msg) : Exception("Test exception: " + msg){};
};

} // namespace common
} // namespace koredb
