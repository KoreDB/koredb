#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API CheckpointException : public Exception {
public:
    explicit CheckpointException(const std::exception& e) : Exception(e.what()){};
};

} // namespace common
} // namespace koredb
