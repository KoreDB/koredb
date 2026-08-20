#pragma once

namespace koredb {
namespace common {
struct DatabaseLifeCycleManager {
    bool isDatabaseClosed = false;
    void checkDatabaseClosedOrThrow() const;
};
} // namespace common
} // namespace koredb
