#pragma once

#include "c_api/koredb.h"
#include "graph_test/base_graph_test.h"

namespace koredb {
namespace testing {

// This class starts database in on-disk mode.
class APIDBTest : public BaseGraphTest {
public:
    void SetUp() override {
        BaseGraphTest::SetUp();
        createDBAndConn();
        initGraph();
    }
};

class CApiTest : public APIDBTest {
public:
    koredb_database _database;
    koredb_connection connection;

    void SetUp() override {
        APIDBTest::SetUp();
        auto* connCppPointer = conn.release();
        auto* databaseCppPointer = database.release();
        connection = koredb_connection{connCppPointer};
        _database = koredb_database{databaseCppPointer};
    }

    std::string getDatabasePath() { return databasePath; }

    koredb_database* getDatabase() { return &_database; }

    koredb_connection* getConnection() { return &connection; }

    void TearDown() override {
        koredb_connection_destroy(&connection);
        koredb_database_destroy(&_database);
        APIDBTest::TearDown();
    }
};

} // namespace testing
} // namespace koredb
