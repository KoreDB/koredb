#include "c_api/koredb.h"
#include "graph_test/base_graph_test.h"
#include "gtest/gtest.h"

using namespace koredb::main;
using namespace koredb::testing;

// This class starts database without initializing graph.
class APIEmptyDBTest : public BaseGraphTest {
    std::string getInputDir() override { KU_UNREACHABLE; }
};

class CApiDatabaseTest : public APIEmptyDBTest {
public:
    void SetUp() override {
        APIEmptyDBTest::SetUp();
        defaultSystemConfig = koredb_default_system_config();

        // limit memory usage by keeping max number of threads small
        defaultSystemConfig.max_num_threads = 2;
        auto maxDBSizeEnv = TestHelper::getSystemEnv("MAX_DB_SIZE");
        if (!maxDBSizeEnv.empty()) {
            defaultSystemConfig.max_db_size = std::stoull(maxDBSizeEnv);
        }
    }

    koredb_system_config defaultSystemConfig;
};

TEST_F(CApiDatabaseTest, CreationAndDestroy) {
    koredb_database database;
    koredb_state state;
    auto databasePathCStr = databasePath.c_str();
    auto systemConfig = defaultSystemConfig;
    state = koredb_database_init(databasePathCStr, systemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(database._database, nullptr);
    auto databaseCpp = static_cast<Database*>(database._database);
    ASSERT_NE(databaseCpp, nullptr);
    koredb_database_destroy(&database);
}

TEST_F(CApiDatabaseTest, CreationReadOnly) {
    koredb_database database;
    koredb_connection connection;
    koredb_query_result queryResult;
    koredb_state state;
    auto databasePathCStr = databasePath.c_str();
    auto systemConfig = defaultSystemConfig;
    // First, create a read-write database.
    state = koredb_database_init(databasePathCStr, systemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(database._database, nullptr);
    auto databaseCpp = static_cast<Database*>(database._database);
    ASSERT_NE(databaseCpp, nullptr);
    koredb_database_destroy(&database);
    // Now, access the same database read-only.
    systemConfig.read_only = true;
    state = koredb_database_init(databasePathCStr, systemConfig, &database);
    if (databasePath == "" || databasePath == ":memory:") {
        ASSERT_EQ(state, KoreDBError);
        ASSERT_EQ(database._database, nullptr);
        return;
    }
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(database._database, nullptr);
    databaseCpp = static_cast<Database*>(database._database);
    ASSERT_NE(databaseCpp, nullptr);
    // Try to write to the database.
    state = koredb_connection_init(&database, &connection);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_connection_query(&connection,
        "CREATE NODE TABLE User(name STRING, age INT64, reg_date DATE, PRIMARY KEY (name))",
        &queryResult);
    ASSERT_EQ(state, KoreDBError);
    ASSERT_FALSE(koredb_query_result_is_success(&queryResult));
    koredb_query_result_destroy(&queryResult);
    koredb_connection_destroy(&connection);
    koredb_database_destroy(&database);
}

TEST_F(CApiDatabaseTest, CreationInMemory) {
    koredb_database database;
    koredb_state state;
    auto databasePathCStr = (char*)"";
    state = koredb_database_init(databasePathCStr, defaultSystemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_database_destroy(&database);
    databasePathCStr = (char*)":memory:";
    state = koredb_database_init(databasePathCStr, defaultSystemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_database_destroy(&database);
}

#ifndef __WASM__ // home directory is not available in WASM
TEST_F(CApiDatabaseTest, CreationHomeDir) {
    koredb_database database;
    koredb_connection connection;
    koredb_state state;
    auto databasePathCStr = (char*)"~/ku_test.db";
    state = koredb_database_init(databasePathCStr, defaultSystemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_connection_init(&database, &connection);
    ASSERT_EQ(state, KoreDBSuccess);
    auto homePath =
        getClientContext(*(Connection*)(connection._connection))->getClientConfig()->homeDirectory;
    koredb_connection_destroy(&connection);
    koredb_database_destroy(&database);
    std::filesystem::remove_all(homePath + "/ku_test.db");
}
#endif

TEST_F(CApiDatabaseTest, CloseQueryResultAndConnectionAfterDatabaseDestroy) {
    koredb_database database;
    auto databasePathCStr = (char*)":memory:";
    auto systemConfig = koredb_default_system_config();
    systemConfig.buffer_pool_size = 10 * 1024 * 1024; // 10MB
    systemConfig.max_db_size = 1 << 30;               // 1GB
    systemConfig.max_num_threads = 2;
    koredb_state state = koredb_database_init(databasePathCStr, systemConfig, &database);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(database._database, nullptr);
    koredb_connection conn;
    koredb_query_result queryResult;
    state = koredb_connection_init(&database, &conn);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_connection_query(&conn, "RETURN 1+1", &queryResult);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&queryResult));
    koredb_flat_tuple tuple;
    koredb_state resultState = koredb_query_result_get_next(&queryResult, &tuple);
    ASSERT_EQ(resultState, KoreDBSuccess);
    koredb_value value;
    koredb_state valueState = koredb_flat_tuple_get_value(&tuple, 0, &value);
    ASSERT_EQ(valueState, KoreDBSuccess);
    int64_t valueInt = INT64_MAX;
    koredb_state valueIntState = koredb_value_get_int64(&value, &valueInt);
    ASSERT_EQ(valueIntState, KoreDBSuccess);
    ASSERT_EQ(valueInt, 2);
    // Destroy database first, this should not crash
    koredb_database_destroy(&database);
    // Call koredb_connection_query should not crash, but return an error
    state = koredb_connection_query(&conn, "RETURN 1+1", &queryResult);
    ASSERT_EQ(state, KoreDBError);
    // Call koredb_query_result_get_next should not crash, but return an error
    resultState = koredb_query_result_get_next(&queryResult, &tuple);
    ASSERT_EQ(resultState, KoreDBError);
    // Now destroy everything, this should not crash
    koredb_query_result_destroy(&queryResult);
    koredb_connection_destroy(&conn);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&tuple);
}

TEST_F(CApiDatabaseTest, UseConnectionAfterDatabaseDestroy) {
    koredb_database db;
    koredb_connection conn;
    koredb_query_result result;

    auto systemConfig = koredb_default_system_config();
    systemConfig.buffer_pool_size = 10 * 1024 * 1024; // 10MB
    systemConfig.max_db_size = 1 << 30;               // 1GB
    systemConfig.max_num_threads = 2;
    auto state = koredb_database_init("", systemConfig, &db);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_connection_init(&db, &conn);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_database_destroy(&db);
    state = koredb_connection_query(&conn, "RETURN 0", &result);
    ASSERT_EQ(state, KoreDBError);

    koredb_connection_destroy(&conn);
}
