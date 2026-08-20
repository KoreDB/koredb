#pragma once

#include "common/api.h"
#include "exception.h"

namespace koredb {
namespace common {

class KOREDB_API CatalogException : public Exception {
public:
    explicit CatalogException(const std::string& msg) : Exception("Catalog exception: " + msg){};
};

} // namespace common
} // namespace koredb
