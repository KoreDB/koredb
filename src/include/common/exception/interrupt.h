#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API InterruptException : public Exception {
public:
    explicit InterruptException() : Exception("Interrupted."){};
};

} // namespace common
} // namespace koredb
