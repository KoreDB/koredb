#pragma once

#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API ExtensionException : public Exception {
public:
    explicit ExtensionException(const std::string& msg)
        : Exception("Extension exception: " + msg) {}
};

} // namespace common
} // namespace koredb
