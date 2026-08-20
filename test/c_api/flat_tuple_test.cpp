#include "c_api_test/c_api_test.h"

using namespace koredb::common;
using namespace koredb::main;
using namespace koredb::testing;

class CApiFlatTupleTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

TEST_F(CApiFlatTupleTest, GetValue) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        "MATCH (a:person) RETURN a.fName, a.age, a.height ORDER BY a.fName LIMIT 1", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_NE(value._value, nullptr);
    auto valueCpp = static_cast<Value*>(value._value);
    ASSERT_NE(valueCpp, nullptr);
    ASSERT_EQ(valueCpp->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(valueCpp->getValue<std::string>(), "Alice");
    koredb_value_destroy(&value);
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 1, &value), KoreDBSuccess);
    ASSERT_NE(value._value, nullptr);
    valueCpp = static_cast<Value*>(value._value);
    ASSERT_NE(valueCpp, nullptr);
    ASSERT_EQ(valueCpp->getDataType().getLogicalTypeID(), LogicalTypeID::INT64);
    ASSERT_EQ(valueCpp->getValue<int64_t>(), 35);
    koredb_value_destroy(&value);
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 2, &value), KoreDBSuccess);
    ASSERT_NE(value._value, nullptr);
    valueCpp = static_cast<Value*>(value._value);
    ASSERT_NE(valueCpp, nullptr);
    ASSERT_EQ(valueCpp->getDataType().getLogicalTypeID(), LogicalTypeID::FLOAT);
    ASSERT_FLOAT_EQ(valueCpp->getValue<float>(), 1.731);
    koredb_value_destroy(&value);
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 222, &value), KoreDBError);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiFlatTupleTest, ToString) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        "MATCH (a:person) RETURN a.fName, a.age, a.height ORDER BY a.fName LIMIT 1", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    auto columnWidths = (uint32_t*)malloc(3 * sizeof(uint32_t));
    columnWidths[0] = 10;
    columnWidths[1] = 5;
    columnWidths[2] = 10;
    char* str = koredb_flat_tuple_to_string(&flatTuple);
    ASSERT_EQ(std::string(str), "Alice|35|1.731000\n");
    koredb_destroy_string(str);
    free(columnWidths);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}
