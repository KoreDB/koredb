#pragma once

#include "attached_database.h"

namespace koredb {
namespace main {

class DatabaseManager {
public:
    DatabaseManager();

    void registerAttachedDatabase(std::unique_ptr<AttachedDatabase> attachedDatabase);
    bool hasAttachedDatabase(const std::string& name);
    KOREDB_API AttachedDatabase* getAttachedDatabase(const std::string& name);
    void detachDatabase(const std::string& databaseName);
    std::string getDefaultDatabase() const { return defaultDatabase; }
    bool hasDefaultDatabase() const { return defaultDatabase != ""; }
    void setDefaultDatabase(const std::string& databaseName);
    std::vector<AttachedDatabase*> getAttachedDatabases() const;
    KOREDB_API void invalidateCache();

private:
    std::vector<std::unique_ptr<AttachedDatabase>> attachedDatabases;
    std::string defaultDatabase;
};

} // namespace main
} // namespace koredb
