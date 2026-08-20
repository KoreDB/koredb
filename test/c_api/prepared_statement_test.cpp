#include "c_api_test/c_api_test.h"

using namespace koredb::main;
using namespace koredb::testing;

class CApiPreparedStatementTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

TEST_F(CApiPreparedStatementTest, IsSuccess) {
    koredb_prepared_statement preparedStatement;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(preparedStatement._prepared_statement, nullptr);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    koredb_prepared_statement_destroy(&preparedStatement);

    query = "MATCH (a:personnnn) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(preparedStatement._prepared_statement, nullptr);
    ASSERT_FALSE(koredb_prepared_statement_is_success(&preparedStatement));
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, GetErrorMessage) {
    koredb_prepared_statement preparedStatement;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(preparedStatement._prepared_statement, nullptr);
    ASSERT_EQ(koredb_prepared_statement_get_error_message(&preparedStatement), nullptr);
    koredb_prepared_statement_destroy(&preparedStatement);

    query = "MATCH (a:personnnn) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(preparedStatement._prepared_statement, nullptr);
    char* message = koredb_prepared_statement_get_error_message(&preparedStatement);
    ASSERT_EQ(std::string(message), "Binder exception: Table personnnn does not exist.");
    koredb_prepared_statement_destroy(&preparedStatement);
    koredb_destroy_string(message);
}

TEST_F(CApiPreparedStatementTest, BindBool) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.isStudent = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_bool(&preparedStatement, "1", true), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 3);
    koredb_query_result_destroy(&result);
    // Bind a different parameter
    ASSERT_EQ(koredb_prepared_statement_bind_bool(&preparedStatement, "1", false), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    resultCpp = static_cast<QueryResult*>(result._query_result);
    tuple = resultCpp->getNext();
    value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 5);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindInt64) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.age > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_int64(&preparedStatement, "1", 30), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 4);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindInt32) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:movies) WHERE a.length > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_int32(&preparedStatement, "1", 200), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindInt16) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.length > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_int16(&preparedStatement, "1", 10), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindInt8) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.level > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_int8(&preparedStatement, "1", 3), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindUInt64) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.code > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(koredb_prepared_statement_bind_uint64(&preparedStatement, "1", 100), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindUInt32) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.temperature> $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_uint32(&preparedStatement, "1", 10), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindUInt16) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.ulength> $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_uint16(&preparedStatement, "1", 100), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindUInt8) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query =
        "MATCH (a:person) -[s:studyAt]-> (b:organisation) WHERE s.ulevel> $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_uint8(&preparedStatement, "1", 14), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 2);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindDouble) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.eyeSight > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_double(&preparedStatement, "1", 4.5), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 7);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindFloat) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.height < $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_float(&preparedStatement, "1", 1.0), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 1);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindString) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.fName = $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    ASSERT_EQ(koredb_prepared_statement_bind_string(&preparedStatement, "1", "Alice"),
        KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 1);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindDate) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.birthdate > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    auto date = koredb_date_t{0};
    ASSERT_EQ(koredb_prepared_statement_bind_date(&preparedStatement, "1", date), KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 4);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindTimestamp) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.registerTime > $1 and cast(a.registerTime, "
                 "\"timestamp_ns\") > $2 and cast(a.registerTime, \"timestamp_ms\") > "
                 "$3 and cast(a.registerTime, \"timestamp_sec\") > $4 and cast(a.registerTime, "
                 "\"timestamp_tz\") > $5 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    auto timestamp = koredb_timestamp_t{0};
    auto timestamp_ns = koredb_timestamp_ns_t{1};
    auto timestamp_ms = koredb_timestamp_ms_t{2};
    auto timestamp_sec = koredb_timestamp_sec_t{3};
    auto timestamp_tz = koredb_timestamp_tz_t{4};
    ASSERT_EQ(koredb_prepared_statement_bind_timestamp(&preparedStatement, "1", timestamp),
        KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_timestamp_ns(&preparedStatement, "2", timestamp_ns),
        KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_timestamp_ms(&preparedStatement, "3", timestamp_ms),
        KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_timestamp_sec(&preparedStatement, "4", timestamp_sec),
        KoreDBSuccess);
    ASSERT_EQ(koredb_prepared_statement_bind_timestamp_tz(&preparedStatement, "5", timestamp_tz),
        KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 7);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindInteval) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.lastJobDuration > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    auto interval = koredb_interval_t{0, 0, 0};
    ASSERT_EQ(koredb_prepared_statement_bind_interval(&preparedStatement, "1", interval),
        KoreDBSuccess);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 8);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}

TEST_F(CApiPreparedStatementTest, BindValue) {
    koredb_prepared_statement preparedStatement;
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    auto query = "MATCH (a:person) WHERE a.registerTime > $1 RETURN COUNT(*)";
    state = koredb_connection_prepare(connection, query, &preparedStatement);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_prepared_statement_is_success(&preparedStatement));
    auto timestamp = koredb_timestamp_t{0};
    auto timestampValue = koredb_value_create_timestamp(timestamp);
    ASSERT_EQ(koredb_prepared_statement_bind_value(&preparedStatement, "1", timestampValue),
        KoreDBSuccess);
    koredb_value_destroy(timestampValue);
    state = koredb_connection_execute(connection, &preparedStatement, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_NE(result._query_result, nullptr);
    ASSERT_EQ(koredb_query_result_get_num_tuples(&result), 1);
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 1);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    auto resultCpp = static_cast<QueryResult*>(result._query_result);
    auto tuple = resultCpp->getNext();
    auto value = tuple->getValue(0)->getValue<int64_t>();
    ASSERT_EQ(value, 7);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&preparedStatement);
}
