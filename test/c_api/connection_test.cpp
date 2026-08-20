#include <thread>

#include "c_api_test/c_api_test.h"

using ::testing::Test;
using namespace koredb::main;
using namespace koredb::testing;

class CApiConnectionTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

TEST_F(CApiConnectionTest, CreationAndDestroy) {
    koredb_connection connection;
    koredb_state state;
    auto _database = getDatabase();
    state = koredb_connection_init(_database, &connection);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(connection._connection, nullptr);
    auto connectionCpp = static_cast<Connection*>(connection._connection);
    ASSERT_NE(connectionCpp, nullptr);
    koredb_connection_destroy(&connection);
}

TEST_F(CApiConnectionTest, CreationAndDestroyWithNullDatabase) {
    koredb_connection connection;
    koredb_state state;
    state = koredb_connection_init(nullptr, &connection);
    ASSERT_EQ(state, KoreDBError);
}

TEST_F(CApiConnectionTest, Query) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) RETURN a.fName";
    state = koredb_connection_query(connection, query, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    ASSERT_NE(resultCpp, nullptr);
    ASSERT_TRUE(resultCpp->isSuccess());
    ASSERT_TRUE(resultCpp->hasNext());
    ASSERT_EQ(resultCpp->getErrorMessage(), "");
    ASSERT_EQ(resultCpp->getNumTuples(), 8);
    ASSERT_EQ(resultCpp->getNumColumns(), 1);
    ASSERT_EQ(resultCpp->getColumnNames()[0], "a.fName");
    koredb_query_result_destroy(&result);
}

TEST_F(CApiConnectionTest, SetGetMaxNumThreadForExec) {
    uint64_t maxNumThreadForExec;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_set_max_num_thread_for_exec(connection, 4);
    ASSERT_EQ(state, KoreDBSuccess);
    auto connectionCpp = static_cast<Connection*>(connection->_connection);
    ASSERT_EQ(connectionCpp->getMaxNumThreadForExec(), 4);
    state = koredb_connection_get_max_num_thread_for_exec(connection, &maxNumThreadForExec);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(maxNumThreadForExec, 4);
    state = koredb_connection_set_max_num_thread_for_exec(connection, 8);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(connectionCpp->getMaxNumThreadForExec(), 8);
    state = koredb_connection_get_max_num_thread_for_exec(connection, &maxNumThreadForExec);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(maxNumThreadForExec, 8);
    koredb_connection badConnection;
    ASSERT_EQ(koredb_connection_init(nullptr, &badConnection), KoreDBError);
    state = koredb_connection_set_max_num_thread_for_exec(&badConnection, 4);
    ASSERT_EQ(state, KoreDBError);
    state = koredb_connection_get_max_num_thread_for_exec(&badConnection, &maxNumThreadForExec);
    ASSERT_EQ(state, KoreDBError);
}

TEST_F(CApiConnectionTest, Prepare) {
    koredb_prepared_statement statement;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &statement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(statement._prepared_statement, nullptr);
    ASSERT_NE(statement._bound_values, nullptr);
    auto statementCpp = static_cast<PreparedStatement*>(statement._prepared_statement);
    ASSERT_NE(statementCpp, nullptr);
    auto connectionCpp = static_cast<Connection*>(connection->_connection);
    auto result = connectionCpp->execute(statementCpp, std::make_pair(std::string("1"), true));
    ASSERT_TRUE(result->isSuccess());
    ASSERT_TRUE(result->hasNext());
    ASSERT_EQ(result->getErrorMessage(), "");
    ASSERT_EQ(result->getNumTuples(), 1);
    auto tuple = result->getNext();
    ASSERT_EQ(tuple->getValue(0)->getValue<int64_t>(), 3);
    koredb_prepared_statement_destroy(&statement);
}

TEST_F(CApiConnectionTest, Execute) {
    koredb_prepared_statement statement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &statement);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_prepared_statement_bind_bool(&statement, "1", true);
    state = koredb_connection_execute(connection, &statement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    ASSERT_NE(resultCpp, nullptr);
    ASSERT_TRUE(resultCpp->isSuccess());
    ASSERT_TRUE(resultCpp->hasNext());
    ASSERT_EQ(resultCpp->getErrorMessage(), "");
    ASSERT_EQ(resultCpp->getNumTuples(), 1);
    auto tuple = resultCpp->getNext();
    ASSERT_EQ(tuple->getValue(0)->getValue<int64_t>(), 3);
    koredb_prepared_statement_destroy(&statement);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiConnectionTest, ExecuteError) {
    koredb_prepared_statement preparedStatement;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(preparedStatement._prepared_statement, nullptr);
    ASSERT_EQ(koredb_prepared_statement_bind_int64(&preparedStatement, "1", 30), KoreDBSuccess);
    koredb_query_result result;
    ASSERT_EQ(koredb_connection_execute(connection, &preparedStatement, &result), KoreDBError);
    ASSERT_FALSE(koredb_query_result_is_success(&result));
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiConnectionTest, QueryTimeout) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    // Disable partial-result-on-timeout (on by default) to exercise the abort-on-timeout path.
    koredb_query_result disableResult;
    ASSERT_EQ(koredb_connection_query(connection, "CALL enable_partial_result_on_timeout=false;",
                  &disableResult),
        KoreDBSuccess);
    koredb_query_result_destroy(&disableResult);
    ASSERT_EQ(koredb_connection_set_query_timeout(connection, 1), KoreDBSuccess);
    state = koredb_connection_query(connection,
        "UNWIND RANGE(1,100000) AS x UNWIND RANGE(1, 100000) AS y RETURN COUNT(x + y);", &result);
    ASSERT_EQ(state, KoreDBError);
    ASSERT_NE(result._query_result, nullptr);
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    ASSERT_NE(resultCpp, nullptr);
    ASSERT_FALSE(resultCpp->isSuccess());
    ASSERT_EQ(resultCpp->getErrorMessage(), "Interrupted.");
    koredb_query_result_destroy(&result);
    koredb_connection badConnection;
    ASSERT_EQ(koredb_connection_init(nullptr, &badConnection), KoreDBError);
    ASSERT_EQ(koredb_connection_set_query_timeout(&badConnection, 1), KoreDBError);
}

TEST_F(CApiConnectionTest, QueryTimeoutPartialResults) {
    auto connection = getConnection();
    koredb_query_result enableResult;
    ASSERT_EQ(koredb_connection_query(connection, "CALL enable_partial_result_on_timeout=true;",
                  &enableResult),
        KoreDBSuccess);
    koredb_query_result_destroy(&enableResult);
    ASSERT_EQ(koredb_connection_set_query_timeout(connection, 1), KoreDBSuccess);
    koredb_query_result result;
    // A partial result is reported as a successful query with is_truncated() == true.
    auto state = koredb_connection_query(connection,
        "UNWIND RANGE(1, 100000) AS x UNWIND RANGE(1, 100000) AS y RETURN x, y;", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_is_truncated(&result));
    koredb_query_result_destroy(&result);
}

#ifndef __SINGLE_THREADED__
// The following test is disabled in single-threaded mode because it requires
// a separate thread to run.
TEST_F(CApiConnectionTest, Interrupt) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    std::atomic<bool> finished = false;

    // Interrupt the query after 100ms
    // This may happen too early, so try again until the query function finishes.
    std::thread t([&connection, &finished]() {
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            koredb_connection_interrupt(connection);
        } while (!finished);
    });
    state = koredb_connection_query(connection,
        "UNWIND RANGE(1,100000) AS x UNWIND RANGE(1, 100000) AS y RETURN COUNT(x + y);", &result);
    finished = true;
    ASSERT_EQ(state, KoreDBError);
    ASSERT_NE(result._query_result, nullptr);
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    ASSERT_NE(resultCpp, nullptr);
    ASSERT_FALSE(resultCpp->isSuccess());
    ASSERT_EQ(resultCpp->getErrorMessage(), "Interrupted.");
    koredb_query_result_destroy(&result);
    t.join();
}
#endif
