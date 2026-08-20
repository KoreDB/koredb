#include <fstream>

#include "c_api_test/c_api_test.h"

using namespace koredb::main;
using namespace koredb::common;
using namespace koredb::processor;
using namespace koredb::testing;

class CApiQueryResultTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

static koredb_value* copy_flat_tuple(koredb_flat_tuple* tuple, uint32_t tupleLen) {
    koredb_value* ret = (koredb_value*)malloc(sizeof(koredb_value) * tupleLen);
    for (uint32_t i = 0; i < tupleLen; i++) {
        koredb_flat_tuple_get_value(tuple, i, &ret[i]);
    }
    return ret;
}

TEST_F(CApiQueryResultTest, GetNextExample) {
    auto conn = getConnection();

    koredb_query_result result;
    koredb_connection_query(conn, "MATCH (p:person) RETURN p.*", &result);

    uint64_t num_tuples = koredb_query_result_get_num_tuples(&result);
    koredb_value** tuples = (koredb_value**)malloc(sizeof(koredb_value*) * num_tuples);
    for (uint64_t i = 0; i < num_tuples; ++i) {
        koredb_flat_tuple tuple;
        koredb_query_result_get_next(&result, &tuple);
        tuples[i] = copy_flat_tuple(&tuple, koredb_query_result_get_num_columns(&result));
        koredb_flat_tuple_destroy(&tuple);
    }

    for (uint64_t i = 0; i < num_tuples; ++i) {
        for (uint64_t j = 0; j < koredb_query_result_get_num_columns(&result); ++j) {
            ASSERT_FALSE(koredb_value_is_null(&tuples[i][j]));
            koredb_value_destroy(&tuples[i][j]);
        }
        free(tuples[i]);
    }

    free((void*)tuples);

    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, GetErrorMessage) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN COUNT(*)", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    char* errorMessage = koredb_query_result_get_error_message(&result);
    koredb_query_result_destroy(&result);

    state = koredb_connection_query(connection, "MATCH (a:personnnn) RETURN COUNT(*)", &result);
    ASSERT_EQ(state, KoreDBError);
    ASSERT_FALSE(koredb_query_result_is_success(&result));
    errorMessage = koredb_query_result_get_error_message(&result);
    ASSERT_EQ(std::string(errorMessage), "Binder exception: Table personnnn does not exist.");
    koredb_query_result_destroy(&result);
    koredb_destroy_string(errorMessage);
}

TEST_F(CApiQueryResultTest, ToString) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN COUNT(*)", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    char* str_repr = koredb_query_result_to_string(&result);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_destroy_string(str_repr);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, GetNumColumns) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN a.fName, a.age, a.height",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_EQ(koredb_query_result_get_num_columns(&result), 3);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, GetColumnName) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN a.fName, a.age, a.height",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    char* columnName;
    ASSERT_EQ(koredb_query_result_get_column_name(&result, 0, &columnName), KoreDBSuccess);
    ASSERT_EQ(std::string(columnName), "a.fName");
    koredb_destroy_string(columnName);
    ASSERT_EQ(koredb_query_result_get_column_name(&result, 1, &columnName), KoreDBSuccess);
    ASSERT_EQ(std::string(columnName), "a.age");
    koredb_destroy_string(columnName);
    ASSERT_EQ(koredb_query_result_get_column_name(&result, 2, &columnName), KoreDBSuccess);
    ASSERT_EQ(std::string(columnName), "a.height");
    koredb_destroy_string(columnName);
    ASSERT_EQ(koredb_query_result_get_column_name(&result, 222, &columnName), KoreDBError);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, GetColumnDataType) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN a.fName, a.age, a.height",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    koredb_logical_type type;
    ASSERT_EQ(koredb_query_result_get_column_data_type(&result, 0, &type), KoreDBSuccess);
    auto typeCpp = (LogicalType*)(type._data_type);
    ASSERT_EQ(typeCpp->getLogicalTypeID(), LogicalTypeID::STRING);
    koredb_data_type_destroy(&type);
    ASSERT_EQ(koredb_query_result_get_column_data_type(&result, 1, &type), KoreDBSuccess);
    typeCpp = (LogicalType*)(type._data_type);
    ASSERT_EQ(typeCpp->getLogicalTypeID(), LogicalTypeID::INT64);
    koredb_data_type_destroy(&type);
    ASSERT_EQ(koredb_query_result_get_column_data_type(&result, 2, &type), KoreDBSuccess);
    typeCpp = (LogicalType*)(type._data_type);
    ASSERT_EQ(typeCpp->getLogicalTypeID(), LogicalTypeID::FLOAT);
    koredb_data_type_destroy(&type);
    ASSERT_EQ(koredb_query_result_get_column_data_type(&result, 222, &type), KoreDBError);
    koredb_query_result_destroy(&result);
}

// TODO(Guodong): Fix this test by adding support of STRUCT in arrow table export.
// TEST_F(CApiQueryResultTest, GetArrowSchema) {
//    auto connection = getConnection();
//    auto result = koredb_connection_query(
//        connection, "MATCH (p:person)-[k:knows]-(q:person) RETURN p.fName, k, q.fName");
//    ASSERT_TRUE(koredb_query_result_is_success(result));
//    auto schema = koredb_query_result_get_arrow_schema(result);
//    ASSERT_STREQ(schema.name, "koredb_query_result");
//    ASSERT_EQ(schema.n_children, 3);
//    ASSERT_STREQ(schema.children[0]->name, "p.fName");
//    ASSERT_STREQ(schema.children[1]->name, "k");
//    ASSERT_STREQ(schema.children[2]->name, "q.fName");
//
//    schema.release(&schema);
//    koredb_query_result_destroy(result);
//}

TEST_F(CApiQueryResultTest, GetQuerySummary) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "MATCH (a:person) RETURN a.fName, a.age, a.height",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    koredb_query_summary summary;
    state = koredb_query_result_get_query_summary(&result, &summary);
    ASSERT_EQ(state, KoreDBSuccess);
    auto compilingTime = koredb_query_summary_get_compiling_time(&summary);
    ASSERT_GT(compilingTime, 0);
    auto executionTime = koredb_query_summary_get_execution_time(&summary);
    ASSERT_GT(executionTime, 0);
    koredb_query_summary_destroy(&summary);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, GetNext) {
    koredb_query_result result;
    koredb_flat_tuple row;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        "MATCH (a:person) RETURN a.fName, a.age ORDER BY a.fName", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));

    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &row);
    ASSERT_EQ(state, KoreDBSuccess);
    auto flatTupleCpp = (FlatTuple*)(row._flat_tuple);
    ASSERT_EQ(flatTupleCpp->getValue(0)->getValue<std::string>(), "Alice");
    ASSERT_EQ(flatTupleCpp->getValue(1)->getValue<int64_t>(), 35);

    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &row);
    ASSERT_EQ(state, KoreDBSuccess);
    flatTupleCpp = (FlatTuple*)(row._flat_tuple);
    ASSERT_EQ(flatTupleCpp->getValue(0)->getValue<std::string>(), "Bob");
    ASSERT_EQ(flatTupleCpp->getValue(1)->getValue<int64_t>(), 30);
    koredb_flat_tuple_destroy(&row);

    while (koredb_query_result_has_next(&result)) {
        koredb_query_result_get_next(&result, &row);
    }
    ASSERT_FALSE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &row);
    ASSERT_EQ(state, KoreDBError);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, ResetIterator) {
    koredb_query_result result;
    koredb_flat_tuple row;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        "MATCH (a:person) RETURN a.fName, a.age ORDER BY a.fName", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));

    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &row);
    ASSERT_EQ(state, KoreDBSuccess);
    auto flatTupleCpp = (FlatTuple*)(row._flat_tuple);
    ASSERT_EQ(flatTupleCpp->getValue(0)->getValue<std::string>(), "Alice");
    ASSERT_EQ(flatTupleCpp->getValue(1)->getValue<int64_t>(), 35);

    koredb_query_result_reset_iterator(&result);

    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &row);
    ASSERT_EQ(state, KoreDBSuccess);
    flatTupleCpp = (FlatTuple*)(row._flat_tuple);
    ASSERT_EQ(flatTupleCpp->getValue(0)->getValue<std::string>(), "Alice");
    ASSERT_EQ(flatTupleCpp->getValue(1)->getValue<int64_t>(), 35);
    koredb_flat_tuple_destroy(&row);

    koredb_query_result_destroy(&result);
}

TEST_F(CApiQueryResultTest, MultipleQuery) {
    koredb_query_result result;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, "return 1; return 2; return 3;", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));

    char* str = koredb_query_result_to_string(&result);
    ASSERT_EQ(std::string(str), "1\n1\n");
    koredb_destroy_string(str);

    ASSERT_TRUE(koredb_query_result_has_next_query_result(&result));
    koredb_query_result next_query_result;
    ASSERT_EQ(koredb_query_result_get_next_query_result(&result, &next_query_result),
        KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&next_query_result));
    str = koredb_query_result_to_string(&next_query_result);
    ASSERT_EQ(std::string(str), "2\n2\n");
    koredb_destroy_string(str);
    koredb_query_result_destroy(&next_query_result);

    ASSERT_EQ(koredb_query_result_get_next_query_result(&result, &next_query_result),
        KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&next_query_result));
    str = koredb_query_result_to_string(&next_query_result);
    ASSERT_EQ(std::string(str), "3\n3\n");
    koredb_destroy_string(str);

    ASSERT_FALSE(koredb_query_result_has_next_query_result(&result));
    ASSERT_EQ(koredb_query_result_get_next_query_result(&result, &next_query_result), KoreDBError);
    koredb_query_result_destroy(&next_query_result);

    koredb_query_result_destroy(&result);
}
