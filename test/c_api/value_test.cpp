#include "c_api_test/c_api_test.h"

using namespace koredb::main;
using namespace koredb::common;
using namespace koredb::testing;

class CApiValueTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

TEST(CApiValueTestEmptyDB, CreateNull) {
    koredb_value* value = koredb_value_create_null();
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::ANY);
    ASSERT_EQ(cppValue->isNull(), true);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateNullWithDatatype) {
    koredb_logical_type type;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &type);
    koredb_value* value = koredb_value_create_null_with_data_type(&type);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    koredb_data_type_destroy(&type);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT64);
    ASSERT_EQ(cppValue->isNull(), true);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, IsNull) {
    koredb_value* value = koredb_value_create_int64(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(value));
    koredb_value_destroy(value);
    value = koredb_value_create_null();
    ASSERT_TRUE(koredb_value_is_null(value));
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, SetNull) {
    koredb_value* value = koredb_value_create_int64(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(value));
    koredb_value_set_null(value, true);
    ASSERT_TRUE(koredb_value_is_null(value));
    koredb_value_set_null(value, false);
    ASSERT_FALSE(koredb_value_is_null(value));
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateDefault) {
    koredb_logical_type type;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &type);
    koredb_value* value = koredb_value_create_default(&type);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    koredb_data_type_destroy(&type);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_FALSE(koredb_value_is_null(value));
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT64);
    ASSERT_EQ(cppValue->getValue<int64_t>(), 0);
    koredb_value_destroy(value);

    koredb_data_type_create(koredb_data_type_id::KOREDB_STRING, nullptr, 0, &type);
    value = koredb_value_create_default(&type);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    koredb_data_type_destroy(&type);
    cppValue = static_cast<Value*>(value->_value);
    ASSERT_FALSE(koredb_value_is_null(value));
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(cppValue->getValue<std::string>(), "");
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateBool) {
    koredb_value* value = koredb_value_create_bool(true);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::BOOL);
    ASSERT_EQ(cppValue->getValue<bool>(), true);
    koredb_value_destroy(value);

    value = koredb_value_create_bool(false);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::BOOL);
    ASSERT_EQ(cppValue->getValue<bool>(), false);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateInt8) {
    koredb_value* value = koredb_value_create_int8(12);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT8);
    ASSERT_EQ(cppValue->getValue<int8_t>(), 12);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateInt16) {
    koredb_value* value = koredb_value_create_int16(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT16);
    ASSERT_EQ(cppValue->getValue<int16_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateInt32) {
    koredb_value* value = koredb_value_create_int32(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT32);
    ASSERT_EQ(cppValue->getValue<int32_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateInt64) {
    koredb_value* value = koredb_value_create_int64(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT64);
    ASSERT_EQ(cppValue->getValue<int64_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateUInt8) {
    koredb_value* value = koredb_value_create_uint8(12);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::UINT8);
    ASSERT_EQ(cppValue->getValue<uint8_t>(), 12);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateUInt16) {
    koredb_value* value = koredb_value_create_uint16(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::UINT16);
    ASSERT_EQ(cppValue->getValue<uint16_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateUInt32) {
    koredb_value* value = koredb_value_create_uint32(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::UINT32);
    ASSERT_EQ(cppValue->getValue<uint32_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateUInt64) {
    koredb_value* value = koredb_value_create_uint64(123);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::UINT64);
    ASSERT_EQ(cppValue->getValue<uint64_t>(), 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateINT128) {
    koredb_value* value = koredb_value_create_int128(koredb_int128_t{211111111, 100000000});
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INT128);
    auto cppTimeStamp = cppValue->getValue<int128_t>();
    ASSERT_EQ(cppTimeStamp.high, 100000000);
    ASSERT_EQ(cppTimeStamp.low, 211111111);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateFloat) {
    koredb_value* value = koredb_value_create_float(123.456);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::FLOAT);
    ASSERT_FLOAT_EQ(cppValue->getValue<float>(), 123.456);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateDouble) {
    koredb_value* value = koredb_value_create_double(123.456);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::DOUBLE);
    ASSERT_DOUBLE_EQ(cppValue->getValue<double>(), 123.456);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateInternalID) {
    auto internalID = koredb_internal_id_t{1, 123};
    koredb_value* value = koredb_value_create_internal_id(internalID);
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INTERNAL_ID);
    auto internalIDCpp = cppValue->getValue<internalID_t>();
    ASSERT_EQ(internalIDCpp.tableID, 1);
    ASSERT_EQ(internalIDCpp.offset, 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateDate) {
    koredb_value* value = koredb_value_create_date(koredb_date_t{123});
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::DATE);
    auto cppDate = cppValue->getValue<date_t>();
    ASSERT_EQ(cppDate.days, 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateTimeStamp) {
    koredb_value* value = koredb_value_create_timestamp(koredb_timestamp_t{123});
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::TIMESTAMP);
    auto cppTimeStamp = cppValue->getValue<timestamp_t>();
    ASSERT_EQ(cppTimeStamp.value, 123);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateTimeStampNonStandard) {
    koredb_value* value_ns = koredb_value_create_timestamp_ns(koredb_timestamp_ns_t{12345});
    koredb_value* value_ms = koredb_value_create_timestamp_ms(koredb_timestamp_ms_t{123456});
    koredb_value* value_sec = koredb_value_create_timestamp_sec(koredb_timestamp_sec_t{1234567});
    koredb_value* value_tz = koredb_value_create_timestamp_tz(koredb_timestamp_tz_t{12345678});

    ASSERT_FALSE(value_ns->_is_owned_by_cpp);
    ASSERT_FALSE(value_ms->_is_owned_by_cpp);
    ASSERT_FALSE(value_sec->_is_owned_by_cpp);
    ASSERT_FALSE(value_tz->_is_owned_by_cpp);
    auto cppValue_ns = static_cast<Value*>(value_ns->_value);
    auto cppValue_ms = static_cast<Value*>(value_ms->_value);
    auto cppValue_sec = static_cast<Value*>(value_sec->_value);
    auto cppValue_tz = static_cast<Value*>(value_tz->_value);
    ASSERT_EQ(cppValue_ns->getDataType().getLogicalTypeID(), LogicalTypeID::TIMESTAMP_NS);
    ASSERT_EQ(cppValue_ms->getDataType().getLogicalTypeID(), LogicalTypeID::TIMESTAMP_MS);
    ASSERT_EQ(cppValue_sec->getDataType().getLogicalTypeID(), LogicalTypeID::TIMESTAMP_SEC);
    ASSERT_EQ(cppValue_tz->getDataType().getLogicalTypeID(), LogicalTypeID::TIMESTAMP_TZ);

    auto cppTimeStamp_ns = cppValue_ns->getValue<timestamp_ns_t>();
    auto cppTimeStamp_ms = cppValue_ms->getValue<timestamp_ms_t>();
    auto cppTimeStamp_sec = cppValue_sec->getValue<timestamp_sec_t>();
    auto cppTimeStamp_tz = cppValue_tz->getValue<timestamp_tz_t>();
    ASSERT_EQ(cppTimeStamp_ns.value, 12345);
    ASSERT_EQ(cppTimeStamp_ms.value, 123456);
    ASSERT_EQ(cppTimeStamp_sec.value, 1234567);
    ASSERT_EQ(cppTimeStamp_tz.value, 12345678);
    koredb_value_destroy(value_ns);
    koredb_value_destroy(value_ms);
    koredb_value_destroy(value_sec);
    koredb_value_destroy(value_tz);
}

TEST(CApiValueTestEmptyDB, CreateInterval) {
    koredb_value* value = koredb_value_create_interval(koredb_interval_t{12, 3, 300});
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::INTERVAL);
    auto cppTimeStamp = cppValue->getValue<interval_t>();
    ASSERT_EQ(cppTimeStamp.months, 12);
    ASSERT_EQ(cppTimeStamp.days, 3);
    ASSERT_EQ(cppTimeStamp.micros, 300);
    koredb_value_destroy(value);
}

TEST(CApiValueTestEmptyDB, CreateString) {
    koredb_value* value = koredb_value_create_string((char*)"abcdefg");
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(cppValue->getValue<std::string>(), "abcdefg");
    koredb_value_destroy(value);
}

TEST_F(CApiValueTest, CreateList) {
    auto connection = getConnection();
    koredb_value* value1 = koredb_value_create_int64(123);
    koredb_value* value2 = koredb_value_create_int64(456);
    koredb_value* value3 = koredb_value_create_int64(789);
    koredb_value* value4 = koredb_value_create_int64(101112);
    koredb_value* value5 = koredb_value_create_int64(131415);
    koredb_value* elements[] = {value1, value2, value3, value4, value5};
    koredb_value* value = nullptr;
    koredb_state state = koredb_value_create_list(5, elements, &value);
    ASSERT_EQ(state, KoreDBSuccess);
    // Destroy the original values, the list should still be valid
    for (int i = 0; i < 5; ++i) {
        koredb_value_destroy(elements[i]);
    }
    ASSERT_FALSE(value->_is_owned_by_cpp);
    koredb_prepared_statement stmt;
    state = koredb_connection_prepare(connection, (char*)"RETURN $1", &stmt);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_prepared_statement_bind_value(&stmt, "1", value);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_query_result result;
    state = koredb_connection_execute(connection, &stmt, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    koredb_flat_tuple flatTuple;
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value outValue;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &outValue), KoreDBSuccess);
    ASSERT_TRUE(outValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&outValue));
    uint64_t size;
    ASSERT_EQ(koredb_value_get_list_size(&outValue, &size), KoreDBSuccess);
    ASSERT_EQ(size, 5);
    koredb_value listElement;
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 0, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    int64_t int64Result;
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 123);
    koredb_value_destroy(&listElement);
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 1, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 456);
    koredb_value_destroy(&listElement);
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 2, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 789);
    koredb_value_destroy(&listElement);
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 3, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 101112);
    koredb_value_destroy(&listElement);
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 4, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 131415);
    koredb_value_destroy(&listElement);
    koredb_value_destroy(&outValue);
    koredb_value_destroy(value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&stmt);
}

TEST(CApiValueTestEmptyDB, CreateListDifferentTypes) {
    koredb_value* value1 = koredb_value_create_int64(123);
    koredb_value* value2 = koredb_value_create_string((char*)"abcdefg");
    koredb_value* elements[] = {value1, value2};
    koredb_value* value = nullptr;
    koredb_state state = koredb_value_create_list(2, elements, &value);
    ASSERT_EQ(state, KoreDBError);
    koredb_value_destroy(value1);
    koredb_value_destroy(value2);
}

TEST(CApiValueTestEmptyDB, CreateListEmpty) {
    koredb_value* elements[] = {nullptr}; // Must be non-empty
    koredb_value* value = nullptr;
    koredb_state state = koredb_value_create_list(0, elements, &value);
    ASSERT_EQ(state, KoreDBError);
}

TEST_F(CApiValueTest, CreateListNested) {
    auto connection = getConnection();
    koredb_value* value1 = koredb_value_create_int64(123);
    koredb_value* value2 = koredb_value_create_int64(456);
    koredb_value* value3 = koredb_value_create_int64(789);
    koredb_value* value4 = koredb_value_create_int64(101112);
    koredb_value* value5 = koredb_value_create_int64(131415);
    koredb_value* elements1[] = {value1, value2, value3};
    koredb_value* elements2[] = {value4, value5};
    koredb_value* list1 = nullptr;
    koredb_value* list2 = nullptr;
    koredb_value_create_list(3, elements1, &list1);
    ASSERT_FALSE(list1->_is_owned_by_cpp);
    koredb_value_create_list(2, elements2, &list2);
    ASSERT_FALSE(list2->_is_owned_by_cpp);
    koredb_value* elements[] = {list1, list2};
    koredb_value* nestedList = nullptr;
    koredb_state state = koredb_value_create_list(2, elements, &nestedList);
    ASSERT_EQ(state, KoreDBSuccess);
    // Destroy the original values, the list should still be valid
    for (int i = 0; i < 3; ++i) {
        koredb_value_destroy(elements1[i]);
    }
    for (int i = 0; i < 2; ++i) {
        koredb_value_destroy(elements2[i]);
    }
    koredb_value_destroy(list1);
    koredb_value_destroy(list2);
    ASSERT_FALSE(nestedList->_is_owned_by_cpp);
    koredb_prepared_statement stmt;
    state = koredb_connection_prepare(connection, (char*)"RETURN $1", &stmt);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_prepared_statement_bind_value(&stmt, "1", nestedList);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_query_result result;
    state = koredb_connection_execute(connection, &stmt, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    koredb_flat_tuple flatTuple;
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value outValue;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &outValue), KoreDBSuccess);
    ASSERT_TRUE(outValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&outValue));
    uint64_t size;
    ASSERT_EQ(koredb_value_get_list_size(&outValue, &size), KoreDBSuccess);
    ASSERT_EQ(size, 2);
    koredb_value listElement;
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 0, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&listElement));
    ASSERT_EQ(koredb_value_get_list_size(&listElement, &size), KoreDBSuccess);
    ASSERT_EQ(size, 3);
    koredb_value innerListElement;
    ASSERT_EQ(koredb_value_get_list_element(&listElement, 0, &innerListElement), KoreDBSuccess);
    ASSERT_TRUE(innerListElement._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&innerListElement));
    int64_t int64Result;
    ASSERT_EQ(koredb_value_get_int64(&innerListElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 123);
    koredb_value_destroy(&innerListElement);
    ASSERT_EQ(koredb_value_get_list_element(&listElement, 1, &innerListElement), KoreDBSuccess);
    ASSERT_TRUE(innerListElement._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&innerListElement));
    ASSERT_EQ(koredb_value_get_int64(&innerListElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 456);
    koredb_value_destroy(&innerListElement);
    ASSERT_EQ(koredb_value_get_list_element(&listElement, 2, &innerListElement), KoreDBSuccess);
    ASSERT_TRUE(innerListElement._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&innerListElement));
    ASSERT_EQ(koredb_value_get_int64(&innerListElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 789);
    koredb_value_destroy(&innerListElement);
    koredb_value_destroy(&listElement);
    ASSERT_EQ(koredb_value_get_list_element(&outValue, 1, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&listElement));
    ASSERT_EQ(koredb_value_get_list_size(&listElement, &size), KoreDBSuccess);
    ASSERT_EQ(size, 2);
    koredb_value_destroy(&listElement);
    koredb_value_destroy(&outValue);
    koredb_value_destroy(nestedList);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&stmt);
}

TEST_F(CApiValueTest, CreateStruct) {
    auto connection = getConnection();
    koredb_value* value1 = koredb_value_create_int16(32);
    koredb_value* value2 = koredb_value_create_string((char*)"Wong");
    koredb_value* value3 = koredb_value_create_string((char*)"Kelley");
    koredb_value* value4 = koredb_value_create_int64(123456);
    koredb_value* value5 = koredb_value_create_string((char*)"CEO");
    koredb_value* value6 = koredb_value_create_bool(true);
    koredb_value* employmentElements[] = {value5, value6};
    const char* employmentFieldNames[] = {(char*)"title", (char*)"is_current"};
    koredb_value* employment = nullptr;
    koredb_state state =
        koredb_value_create_struct(2, employmentFieldNames, employmentElements, &employment);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_FALSE(employment->_is_owned_by_cpp);
    koredb_value_destroy(value5);
    koredb_value_destroy(value6);
    koredb_value* personElements[] = {value1, value2, value3, value4, employment};
    const char* personFieldNames[] = {(char*)"age", (char*)"first_name", (char*)"last_name",
        (char*)"id", (char*)"employment"};
    koredb_value* person = nullptr;
    state = koredb_value_create_struct(5, personFieldNames, personElements, &person);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value_destroy(value1);
    koredb_value_destroy(value2);
    koredb_value_destroy(value3);
    koredb_value_destroy(value4);
    koredb_value_destroy(employment);
    ASSERT_FALSE(person->_is_owned_by_cpp);
    koredb_prepared_statement stmt;
    state = koredb_connection_prepare(connection, (char*)"RETURN $1", &stmt);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_prepared_statement_bind_value(&stmt, "1", person);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_query_result result;
    state = koredb_connection_execute(connection, &stmt, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    koredb_flat_tuple flatTuple;
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value outValue;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &outValue), KoreDBSuccess);
    ASSERT_TRUE(outValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&outValue));
    uint64_t size;
    state = koredb_value_get_struct_num_fields(&outValue, &size);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(size, 5);
    char* structFieldName;
    koredb_value structFieldValue;
    state = koredb_value_get_struct_field_name(&outValue, 0, &structFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(structFieldName, "age");
    state = koredb_value_get_struct_field_value(&outValue, 0, &structFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(structFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&structFieldValue));
    int16_t int16Result;
    state = koredb_value_get_int16(&structFieldValue, &int16Result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(int16Result, 32);
    koredb_value_destroy(&structFieldValue);
    koredb_destroy_string(structFieldName);
    state = koredb_value_get_struct_field_name(&outValue, 1, &structFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(structFieldName, "first_name");
    state = koredb_value_get_struct_field_value(&outValue, 1, &structFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(structFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&structFieldValue));
    char* stringResult;
    state = koredb_value_get_string(&structFieldValue, &stringResult);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(stringResult, "Wong");
    koredb_value_destroy(&structFieldValue);
    koredb_destroy_string(structFieldName);
    koredb_destroy_string(stringResult);
    state = koredb_value_get_struct_field_name(&outValue, 2, &structFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(structFieldName, "last_name");
    state = koredb_value_get_struct_field_value(&outValue, 2, &structFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(structFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&structFieldValue));
    state = koredb_value_get_string(&structFieldValue, &stringResult);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(stringResult, "Kelley");
    koredb_value_destroy(&structFieldValue);
    koredb_destroy_string(structFieldName);
    koredb_destroy_string(stringResult);
    state = koredb_value_get_struct_field_name(&outValue, 3, &structFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(structFieldName, "id");
    state = koredb_value_get_struct_field_value(&outValue, 3, &structFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(structFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&structFieldValue));
    int64_t int64Result;
    state = koredb_value_get_int64(&structFieldValue, &int64Result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(int64Result, 123456);
    koredb_value_destroy(&structFieldValue);
    koredb_destroy_string(structFieldName);
    state = koredb_value_get_struct_field_name(&outValue, 4, &structFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(structFieldName, "employment");
    state = koredb_value_get_struct_field_value(&outValue, 4, &structFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(structFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&structFieldValue));
    state = koredb_value_get_struct_num_fields(&structFieldValue, &size);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(size, 2);
    char* employmentFieldName;
    koredb_value employmentFieldValue;
    state = koredb_value_get_struct_field_name(&structFieldValue, 0, &employmentFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(employmentFieldName, "title");
    state = koredb_value_get_struct_field_value(&structFieldValue, 0, &employmentFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(employmentFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&employmentFieldValue));
    state = koredb_value_get_string(&employmentFieldValue, &stringResult);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(stringResult, "CEO");
    koredb_value_destroy(&employmentFieldValue);
    koredb_destroy_string(employmentFieldName);
    koredb_destroy_string(stringResult);
    state = koredb_value_get_struct_field_name(&structFieldValue, 1, &employmentFieldName);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_STREQ(employmentFieldName, "is_current");
    state = koredb_value_get_struct_field_value(&structFieldValue, 1, &employmentFieldValue);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(employmentFieldValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&employmentFieldValue));
    bool boolResult;
    state = koredb_value_get_bool(&employmentFieldValue, &boolResult);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_EQ(boolResult, true);
    koredb_value_destroy(&employmentFieldValue);
    koredb_destroy_string(employmentFieldName);
    koredb_value_destroy(&structFieldValue);
    koredb_destroy_string(structFieldName);
    koredb_value_destroy(&outValue);
    koredb_value_destroy(person);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&stmt);
}

TEST(CApiValueTestEmptyDB, CreateStructEmpty) {
    const char* fieldNames[] = {(char*)"name"}; // Must be non-empty
    koredb_value* values[] = {nullptr};         // Must be non-empty
    koredb_value* value = nullptr;
    koredb_state state = koredb_value_create_struct(0, fieldNames, values, &value);
    ASSERT_EQ(state, KoreDBError);
}

TEST_F(CApiValueTest, CreateMap) {
    auto connection = getConnection();
    koredb_value* key1 = koredb_value_create_int64(1);
    koredb_value* value1 = koredb_value_create_string((char*)"one");
    koredb_value* key2 = koredb_value_create_int64(2);
    koredb_value* value2 = koredb_value_create_string((char*)"two");
    koredb_value* key3 = koredb_value_create_int64(3);
    koredb_value* value3 = koredb_value_create_string((char*)"three");
    koredb_value* keys[] = {key1, key2, key3};
    koredb_value* values[] = {value1, value2, value3};
    koredb_value* map = nullptr;
    koredb_state state = koredb_value_create_map(3, keys, values, &map);
    ASSERT_EQ(state, KoreDBSuccess);
    // Destroy the original values, the map should still be valid
    for (int i = 0; i < 3; ++i) {
        koredb_value_destroy(keys[i]);
        koredb_value_destroy(values[i]);
    }
    ASSERT_FALSE(map->_is_owned_by_cpp);
    koredb_prepared_statement stmt;
    state = koredb_connection_prepare(connection, (char*)"RETURN $1", &stmt);
    ASSERT_EQ(state, KoreDBSuccess);
    state = koredb_prepared_statement_bind_value(&stmt, "1", map);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_query_result result;
    state = koredb_connection_execute(connection, &stmt, &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    koredb_flat_tuple flatTuple;
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value outValue;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &outValue), KoreDBSuccess);
    ASSERT_TRUE(outValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&outValue));
    uint64_t size;
    ASSERT_EQ(koredb_value_get_map_size(&outValue, &size), KoreDBSuccess);
    ASSERT_EQ(size, 3);
    koredb_value mapValue;
    ASSERT_EQ(koredb_value_get_map_value(&outValue, 0, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    char* stringResult;
    ASSERT_EQ(koredb_value_get_string(&mapValue, &stringResult), KoreDBSuccess);
    ASSERT_STREQ(stringResult, "one");
    koredb_value_destroy(&mapValue);
    koredb_destroy_string(stringResult);
    ASSERT_EQ(koredb_value_get_map_value(&outValue, 1, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    ASSERT_EQ(koredb_value_get_string(&mapValue, &stringResult), KoreDBSuccess);
    ASSERT_STREQ(stringResult, "two");
    koredb_value_destroy(&mapValue);
    koredb_destroy_string(stringResult);
    ASSERT_EQ(koredb_value_get_map_value(&outValue, 2, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    ASSERT_EQ(koredb_value_get_string(&mapValue, &stringResult), KoreDBSuccess);
    ASSERT_STREQ(stringResult, "three");
    koredb_value_destroy(&mapValue);
    koredb_destroy_string(stringResult);
    ASSERT_EQ(koredb_value_get_map_key(&outValue, 0, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    int64_t int64Result;
    ASSERT_EQ(koredb_value_get_int64(&mapValue, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 1);
    koredb_value_destroy(&mapValue);
    ASSERT_EQ(koredb_value_get_map_key(&outValue, 1, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    ASSERT_EQ(koredb_value_get_int64(&mapValue, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 2);
    koredb_value_destroy(&mapValue);
    ASSERT_EQ(koredb_value_get_map_key(&outValue, 2, &mapValue), KoreDBSuccess);
    ASSERT_TRUE(mapValue._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&mapValue));
    ASSERT_EQ(koredb_value_get_int64(&mapValue, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 3);
    koredb_value_destroy(&mapValue);
    koredb_value_destroy(&outValue);
    koredb_value_destroy(map);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
    koredb_prepared_statement_destroy(&stmt);
}

TEST(CApiValueTestEmptyDB, CreateMapEmpty) {
    koredb_value* keys[] = {nullptr};   // Must be non-empty
    koredb_value* values[] = {nullptr}; // Must be non-empty
    koredb_value* map = nullptr;
    koredb_state state = koredb_value_create_map(0, keys, values, &map);
    ASSERT_EQ(state, KoreDBError);
}

TEST(CApiValueTestEmptyDB, Clone) {
    koredb_value* value = koredb_value_create_string((char*)"abcdefg");
    ASSERT_FALSE(value->_is_owned_by_cpp);
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(cppValue->getValue<std::string>(), "abcdefg");

    koredb_value* clone = koredb_value_clone(value);
    koredb_value_destroy(value);

    ASSERT_FALSE(clone->_is_owned_by_cpp);
    auto cppClone = static_cast<Value*>(clone->_value);
    ASSERT_EQ(cppClone->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(cppClone->getValue<std::string>(), "abcdefg");
    koredb_value_destroy(clone);
}

TEST(CApiValueTestEmptyDB, Copy) {
    koredb_value* value = koredb_value_create_string((char*)"abc");

    koredb_value* value2 = koredb_value_create_string((char*)"abcdefg");
    koredb_value_copy(value, value2);
    koredb_value_destroy(value2);

    ASSERT_FALSE(koredb_value_is_null(value));
    auto cppValue = static_cast<Value*>(value->_value);
    ASSERT_EQ(cppValue->getDataType().getLogicalTypeID(), LogicalTypeID::STRING);
    ASSERT_EQ(cppValue->getValue<std::string>(), "abcdefg");
    koredb_value_destroy(value);
}

TEST_F(CApiValueTest, GetListSize) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.workedHours ORDER BY a.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint64_t size;
    ASSERT_EQ(koredb_value_get_list_size(&value, &size), KoreDBSuccess);
    ASSERT_EQ(size, 2);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_list_size(badValue, &size), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetListElement) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.workedHours ORDER BY a.ID", &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint64_t size;
    ASSERT_EQ(koredb_value_get_list_size(&value, &size), KoreDBSuccess);
    ASSERT_EQ(size, 2);

    koredb_value listElement;
    ASSERT_EQ(koredb_value_get_list_element(&value, 0, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    int64_t int64Result;
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 10);

    ASSERT_EQ(koredb_value_get_list_element(&value, 1, &listElement), KoreDBSuccess);
    ASSERT_TRUE(listElement._is_owned_by_cpp);
    ASSERT_EQ(koredb_value_get_int64(&listElement, &int64Result), KoreDBSuccess);
    ASSERT_EQ(int64Result, 5);
    koredb_value_destroy(&listElement);

    ASSERT_EQ(koredb_value_get_list_element(&value, 222, &listElement), KoreDBError);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, GetStructNumFields) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.name=\"Roma\" RETURN m.description", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    koredb_flat_tuple_get_value(&flatTuple, 0, &value);
    uint64_t numFields;
    ASSERT_EQ(koredb_value_get_struct_num_fields(&value, &numFields), KoreDBSuccess);
    ASSERT_EQ(numFields, 14);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_struct_num_fields(badValue, &numFields), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetStructFieldName) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.name=\"Roma\" RETURN m.description", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    char* fieldName;
    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 0, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "rating");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 1, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "stars");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 2, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "views");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 3, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "release");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 4, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "release_ns");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 5, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "release_ms");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 6, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "release_sec");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 7, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "release_tz");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 8, &fieldName), KoreDBSuccess);
    ASSERT_STREQ(fieldName, "film");
    koredb_destroy_string(fieldName);

    ASSERT_EQ(koredb_value_get_struct_field_name(&value, 222, &fieldName), KoreDBError);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, GetStructFieldValue) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.name=\"Roma\" RETURN m.description", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);

    koredb_value fieldValue;
    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 0, &fieldValue), KoreDBSuccess);
    koredb_logical_type fieldType;
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_DOUBLE);
    double doubleValue;
    ASSERT_EQ(koredb_value_get_double(&fieldValue, &doubleValue), KoreDBSuccess);
    ASSERT_DOUBLE_EQ(doubleValue, 1223);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 1, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 2, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_INT64);
    int64_t int64Value;
    ASSERT_EQ(koredb_value_get_int64(&fieldValue, &int64Value), KoreDBSuccess);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 3, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_TIMESTAMP);
    koredb_timestamp_t timestamp;
    ASSERT_EQ(koredb_value_get_timestamp(&fieldValue, &timestamp), KoreDBSuccess);
    ASSERT_EQ(timestamp.value, 1297442662000000);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 4, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_TIMESTAMP_NS);
    koredb_timestamp_ns_t timestamp_ns;
    ASSERT_EQ(koredb_value_get_timestamp_ns(&fieldValue, &timestamp_ns), KoreDBSuccess);
    ASSERT_EQ(timestamp_ns.value, 1297442662123456000);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 5, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_TIMESTAMP_MS);
    koredb_timestamp_ms_t timestamp_ms;
    ASSERT_EQ(koredb_value_get_timestamp_ms(&fieldValue, &timestamp_ms), KoreDBSuccess);
    ASSERT_EQ(timestamp_ms.value, 1297442662123);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 6, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_TIMESTAMP_SEC);
    koredb_timestamp_sec_t timestamp_sec;
    ASSERT_EQ(koredb_value_get_timestamp_sec(&fieldValue, &timestamp_sec), KoreDBSuccess);
    ASSERT_EQ(timestamp_sec.value, 1297442662);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 7, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_TIMESTAMP_TZ);
    koredb_timestamp_tz_t timestamp_tz;
    ASSERT_EQ(koredb_value_get_timestamp_tz(&fieldValue, &timestamp_tz), KoreDBSuccess);
    ASSERT_EQ(timestamp_tz.value, 1297442662123456);
    koredb_data_type_destroy(&fieldType);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 8, &fieldValue), KoreDBSuccess);
    koredb_value_get_data_type(&fieldValue, &fieldType);
    ASSERT_EQ(koredb_data_type_get_id(&fieldType), KOREDB_DATE);
    koredb_date_t date;
    ASSERT_EQ(koredb_value_get_date(&fieldValue, &date), KoreDBSuccess);
    ASSERT_EQ(date.days, 15758);
    koredb_data_type_destroy(&fieldType);
    koredb_value_destroy(&fieldValue);

    ASSERT_EQ(koredb_value_get_struct_field_value(&value, 222, &fieldValue), KoreDBError);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, getMapNumFields) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.length = 2544 RETURN m.audience", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_FALSE(koredb_query_result_has_next(&result));
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);

    uint64_t mapFields;
    ASSERT_EQ(koredb_value_get_map_size(&value, &mapFields), KoreDBSuccess);
    ASSERT_EQ(mapFields, 1);

    koredb_query_result_destroy(&result);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
}

TEST_F(CApiValueTest, getMapKey) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.length = 2544 RETURN m.audience", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_FALSE(koredb_query_result_has_next(&result));
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);

    koredb_value key;
    ASSERT_EQ(koredb_value_get_map_key(&value, 0, &key), KoreDBSuccess);
    koredb_logical_type keyType;
    koredb_value_get_data_type(&key, &keyType);
    ASSERT_EQ(koredb_data_type_get_id(&keyType), KOREDB_STRING);
    char* mapName;
    ASSERT_EQ(koredb_value_get_string(&key, &mapName), KoreDBSuccess);
    ASSERT_STREQ(mapName, "audience1");
    koredb_destroy_string(mapName);
    koredb_data_type_destroy(&keyType);
    koredb_value_destroy(&key);

    ASSERT_EQ(koredb_value_get_map_key(&value, 1, &key), KoreDBError);
    koredb_query_result_destroy(&result);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
}

TEST_F(CApiValueTest, getMapValue) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) WHERE m.length = 2544 RETURN m.audience", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_FALSE(koredb_query_result_has_next(&result));
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);

    koredb_value mapValue;
    ASSERT_EQ(koredb_value_get_map_value(&value, 0, &mapValue), KoreDBSuccess);
    koredb_logical_type mapType;
    koredb_value_get_data_type(&mapValue, &mapType);
    ASSERT_EQ(koredb_data_type_get_id(&mapType), KOREDB_INT64);
    int64_t mapIntValue;
    ASSERT_EQ(koredb_value_get_int64(&mapValue, &mapIntValue), KoreDBSuccess);
    ASSERT_EQ(mapIntValue, 33);

    ASSERT_EQ(koredb_value_get_map_value(&value, 1, &mapValue), KoreDBError);

    koredb_data_type_destroy(&mapType);
    koredb_query_result_destroy(&result);
    koredb_value_destroy(&mapValue);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
}

TEST_F(CApiValueTest, getDecimalAsString) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"UNWIND [1] AS A UNWIND [5.7, 8.3, 8.7, 13.7] AS B WITH cast(CAST(A AS DECIMAL) "
               "* "
               "CAST(B AS DECIMAL) AS DECIMAL(18, 1)) AS PROD RETURN COLLECT(PROD) AS RES",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);

    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);

    koredb_logical_type dataType;
    koredb_value_get_data_type(&value, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_LIST);
    uint64_t list_size;
    ASSERT_EQ(koredb_value_get_list_size(&value, &list_size), KoreDBSuccess);
    ASSERT_EQ(list_size, 4);
    koredb_data_type_destroy(&dataType);

    koredb_value decimal_entry;
    char* decimal_value;
    std::string decimal_string_value;
    ASSERT_EQ(koredb_value_get_list_element(&value, 0, &decimal_entry), KoreDBSuccess);
    koredb_value_get_data_type(&decimal_entry, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_DECIMAL);
    ASSERT_EQ(koredb_value_get_decimal_as_string(&decimal_entry, &decimal_value), KoreDBSuccess);
    decimal_string_value = std::string(decimal_value);
    ASSERT_EQ(decimal_string_value, "5.7");
    koredb_destroy_string(decimal_value);
    koredb_data_type_destroy(&dataType);

    ASSERT_EQ(koredb_value_get_list_element(&value, 1, &decimal_entry), KoreDBSuccess);
    koredb_value_get_data_type(&decimal_entry, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_DECIMAL);
    ASSERT_EQ(koredb_value_get_decimal_as_string(&decimal_entry, &decimal_value), KoreDBSuccess);
    decimal_string_value = std::string(decimal_value);
    ASSERT_EQ(decimal_string_value, "8.3");
    koredb_destroy_string(decimal_value);
    koredb_data_type_destroy(&dataType);

    ASSERT_EQ(koredb_value_get_list_element(&value, 2, &decimal_entry), KoreDBSuccess);
    koredb_value_get_data_type(&decimal_entry, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_DECIMAL);
    ASSERT_EQ(koredb_value_get_decimal_as_string(&decimal_entry, &decimal_value), KoreDBSuccess);
    decimal_string_value = std::string(decimal_value);
    ASSERT_EQ(decimal_string_value, "8.7");
    koredb_destroy_string(decimal_value);
    koredb_data_type_destroy(&dataType);

    ASSERT_EQ(koredb_value_get_list_element(&value, 3, &decimal_entry), KoreDBSuccess);
    koredb_value_get_data_type(&decimal_entry, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_DECIMAL);
    ASSERT_EQ(koredb_value_get_decimal_as_string(&decimal_entry, &decimal_value), KoreDBSuccess);
    decimal_string_value = std::string(decimal_value);
    ASSERT_EQ(decimal_string_value, "13.7");
    koredb_destroy_string(decimal_value);
    koredb_data_type_destroy(&dataType);

    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
    koredb_value_destroy(&decimal_entry);
}

TEST_F(CApiValueTest, GetDataType) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.fName, a.isStudent, a.workedHours", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    koredb_logical_type dataType;
    koredb_value_get_data_type(&value, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_STRING);
    koredb_data_type_destroy(&dataType);

    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 1, &value), KoreDBSuccess);
    koredb_value_get_data_type(&value, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_BOOL);
    koredb_data_type_destroy(&dataType);

    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 2, &value), KoreDBSuccess);
    koredb_value_get_data_type(&value, &dataType);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), KOREDB_LIST);
    koredb_data_type_destroy(&dataType);
    koredb_value_destroy(&value);

    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, GetBool) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.isStudent ORDER BY a.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    bool boolValue;
    ASSERT_EQ(koredb_value_get_bool(&value, &boolValue), KoreDBSuccess);
    ASSERT_TRUE(boolValue);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_bool(badValue, &boolValue), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInt8) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.level ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    int8_t int8Value;
    ASSERT_EQ(koredb_value_get_int8(&value, &int8Value), KoreDBSuccess);
    ASSERT_EQ(int8Value, 5);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_int8(badValue, &int8Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInt16) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.length ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    int16_t int16Value;
    ASSERT_EQ(koredb_value_get_int16(&value, &int16Value), KoreDBSuccess);
    ASSERT_EQ(int16Value, 5);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_int16(badValue, &int16Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInt32) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (m:movies) RETURN m.length ORDER BY m.name", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    int32_t int32Value;
    ASSERT_EQ(koredb_value_get_int32(&value, &int32Value), KoreDBSuccess);
    ASSERT_EQ(int32Value, 298);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_int32(badValue, &int32Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInt64) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a.ID ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    int64_t int64Value;
    ASSERT_EQ(koredb_value_get_int64(&value, &int64Value), KoreDBSuccess);
    ASSERT_EQ(int64Value, 0);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_int64(badValue, &int64Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetUInt8) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.ulevel ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint8_t uint8Value;
    ASSERT_EQ(koredb_value_get_uint8(&value, &uint8Value), KoreDBSuccess);
    ASSERT_EQ(uint8Value, 250);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_uint8(badValue, &uint8Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetUInt16) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.ulength ORDER BY "
               "a.ID",
        &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint16_t uint16Value;
    ASSERT_EQ(koredb_value_get_uint16(&value, &uint16Value), KoreDBSuccess);
    ASSERT_EQ(uint16Value, 33768);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_uint16(badValue, &uint16Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetUInt32) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) "
               "RETURN r.temperature ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint32_t uint32Value;
    ASSERT_EQ(koredb_value_get_uint32(&value, &uint32Value), KoreDBSuccess);
    ASSERT_EQ(uint32Value, 32800);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_uint32(badValue, &uint32Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetUInt64) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.code ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint64_t uint64Value;
    ASSERT_EQ(koredb_value_get_uint64(&value, &uint64Value), KoreDBSuccess);
    ASSERT_EQ(uint64Value, 9223372036854775808ull);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_uint64(badValue, &uint64Value), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInt128) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:studyAt]-> (b:organisation) RETURN r.hugedata ORDER BY "
               "a.ID",
        &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    koredb_int128_t int128;
    ASSERT_EQ(koredb_value_get_int128(&value, &int128), KoreDBSuccess);
    ASSERT_EQ(int128.high, 100000000);
    ASSERT_EQ(int128.low, 211111111);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_int128(badValue, &int128), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, StringToInt128Test) {
    char input[] = "1844674407370955161811111111";
    koredb_int128_t int128_val;
    ASSERT_EQ(koredb_int128_t_from_string(input, &int128_val), KoreDBSuccess);
    ASSERT_EQ(int128_val.high, 100000000);
    ASSERT_EQ(int128_val.low, 211111111);

    char badInput[] = "this is not a int128";
    koredb_int128_t int128_val2;
    ASSERT_EQ(koredb_int128_t_from_string(badInput, &int128_val2), KoreDBError);
}

TEST_F(CApiValueTest, Int128ToStringTest) {
    auto int128_val = koredb_int128_t{211111111, 100000000};
    char* str;
    ASSERT_EQ(koredb_int128_t_to_string(int128_val, &str), KoreDBSuccess);
    ASSERT_STREQ(str, "1844674407370955161811111111");
    koredb_destroy_string(str);
}

TEST_F(CApiValueTest, GetFloat) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.height ORDER BY a.ID", &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    float floatValue;
    ASSERT_EQ(koredb_value_get_float(&value, &floatValue), KoreDBSuccess);
    ASSERT_FLOAT_EQ(floatValue, 1.731);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_float(badValue, &floatValue), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetDouble) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.eyeSight ORDER BY a.ID", &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    double doubleValue;
    ASSERT_EQ(koredb_value_get_double(&value, &doubleValue), KoreDBSuccess);
    ASSERT_DOUBLE_EQ(doubleValue, 5.0);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_double(badValue, &doubleValue), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInternalID) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a ORDER BY a.ID",
        &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    koredb_value nodeIDVal;
    ASSERT_EQ(
        koredb_value_get_struct_field_value(&value, 0 /* internal ID field idx */, &nodeIDVal),
        KoreDBSuccess);
    koredb_internal_id_t internalID;
    ASSERT_EQ(koredb_value_get_internal_id(&nodeIDVal, &internalID), KoreDBSuccess);
    ASSERT_EQ(internalID.table_id, 0);
    ASSERT_EQ(internalID.offset, 0);
    koredb_value_destroy(&nodeIDVal);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_internal_id(badValue, &internalID), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetRelVal) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[r:knows]-> (b:person) RETURN r ORDER BY a.ID, b.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value rel;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &rel), KoreDBSuccess);
    ASSERT_TRUE(rel._is_owned_by_cpp);
    koredb_value relIdVal;
    ASSERT_EQ(koredb_rel_val_get_id_val(&rel, &relIdVal), KoreDBSuccess);
    koredb_internal_id_t relInternalID;
    ASSERT_EQ(koredb_value_get_internal_id(&relIdVal, &relInternalID), KoreDBSuccess);
    ASSERT_EQ(relInternalID.table_id, 3);
    ASSERT_EQ(relInternalID.offset, 0);
    koredb_value relSrcIDVal;
    ASSERT_EQ(koredb_rel_val_get_src_id_val(&rel, &relSrcIDVal), KoreDBSuccess);
    koredb_internal_id_t relSrcID;
    ASSERT_EQ(koredb_value_get_internal_id(&relSrcIDVal, &relSrcID), KoreDBSuccess);
    ASSERT_EQ(relSrcID.table_id, 0);
    ASSERT_EQ(relSrcID.offset, 0);
    koredb_value relDstIDVal;
    ASSERT_EQ(koredb_rel_val_get_dst_id_val(&rel, &relDstIDVal), KoreDBSuccess);
    koredb_internal_id_t relDstID;
    ASSERT_EQ(koredb_value_get_internal_id(&relDstIDVal, &relDstID), KoreDBSuccess);
    ASSERT_EQ(relDstID.table_id, 0);
    ASSERT_EQ(relDstID.offset, 1);
    koredb_value relLabel;
    ASSERT_EQ(koredb_rel_val_get_label_val(&rel, &relLabel), KoreDBSuccess);
    char* relLabelStr;
    ASSERT_EQ(koredb_value_get_string(&relLabel, &relLabelStr), KoreDBSuccess);
    ASSERT_STREQ(relLabelStr, "knows");
    uint64_t propertiesSize;
    ASSERT_EQ(koredb_rel_val_get_property_size(&rel, &propertiesSize), KoreDBSuccess);
    ASSERT_EQ(propertiesSize, 7);
    koredb_destroy_string(relLabelStr);
    koredb_value_destroy(&relLabel);
    koredb_value_destroy(&relIdVal);
    koredb_value_destroy(&relSrcIDVal);
    koredb_value_destroy(&relDstIDVal);
    koredb_value_destroy(&rel);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_rel_val_get_src_id_val(badValue, &relSrcIDVal), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetDate) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.birthdate ORDER BY a.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    koredb_date_t date;
    ASSERT_EQ(koredb_value_get_date(&value, &date), KoreDBSuccess);
    ASSERT_EQ(date.days, -25567);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_date(badValue, &date), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetTimestamp) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.registerTime ORDER BY a.ID", &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    koredb_timestamp_t timestamp;
    ASSERT_EQ(koredb_value_get_timestamp(&value, &timestamp), KoreDBSuccess);
    ASSERT_EQ(timestamp.value, 1313839530000000);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_timestamp(badValue, &timestamp), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetInterval) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.lastJobDuration ORDER BY a.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    koredb_interval_t interval;
    ASSERT_EQ(koredb_value_get_interval(&value, &interval), KoreDBSuccess);
    ASSERT_EQ(interval.months, 36);
    ASSERT_EQ(interval.days, 2);
    ASSERT_EQ(interval.micros, 46920000000);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_interval(badValue, &interval), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetString) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.fName ORDER BY a.ID", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    char* str;
    ASSERT_EQ(koredb_value_get_string(&value, &str), KoreDBSuccess);
    ASSERT_STREQ(str, "Alice");
    koredb_destroy_string(str);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_int32(123);
    ASSERT_EQ(koredb_value_get_string(badValue, &str), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetBlob) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)R"(RETURN BLOB('\\xAA\\xBB\\xCD\\x1A');)",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    uint8_t* blob;
    ASSERT_EQ(koredb_value_get_blob(&value, &blob), KoreDBSuccess);
    ASSERT_EQ(blob[0], 0xAA);
    ASSERT_EQ(blob[1], 0xBB);
    ASSERT_EQ(blob[2], 0xCD);
    ASSERT_EQ(blob[3], 0x1A);
    ASSERT_EQ(blob[4], 0x00);
    koredb_destroy_blob(blob);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_blob(badValue, &blob), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetUUID) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)R"(RETURN UUID("A0EEBC99-9C0B-4EF8-BB6D-6BB9BD380A11");)", &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value value;
    koredb_flat_tuple_get_value(&flatTuple, 0, &value);
    ASSERT_TRUE(value._is_owned_by_cpp);
    ASSERT_FALSE(koredb_value_is_null(&value));
    char* str;
    ASSERT_EQ(koredb_value_get_uuid(&value, &str), KoreDBSuccess);
    ASSERT_STREQ(str, "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11");
    koredb_destroy_string(str);
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_value_get_uuid(badValue, &str), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, ToSting) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) RETURN a.fName, a.isStudent, a.workedHours ORDER BY "
               "a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));

    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);

    koredb_value value;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &value), KoreDBSuccess);
    char* str = koredb_value_to_string(&value);
    ASSERT_STREQ(str, "Alice");
    koredb_destroy_string(str);

    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 1, &value), KoreDBSuccess);
    str = koredb_value_to_string(&value);
    ASSERT_STREQ(str, "True");
    koredb_destroy_string(str);

    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 2, &value), KoreDBSuccess);
    str = koredb_value_to_string(&value);
    ASSERT_STREQ(str, "[10,5]");
    koredb_destroy_string(str);
    koredb_value_destroy(&value);

    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, NodeValGetLabelVal) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));

    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value nodeVal;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &nodeVal), KoreDBSuccess);
    koredb_value labelVal;
    ASSERT_EQ(koredb_node_val_get_label_val(&nodeVal, &labelVal), KoreDBSuccess);
    char* labelStr;
    ASSERT_EQ(koredb_value_get_string(&labelVal, &labelStr), KoreDBSuccess);
    ASSERT_STREQ(labelStr, "person");
    koredb_destroy_string(labelStr);
    koredb_value_destroy(&labelVal);
    koredb_value_destroy(&nodeVal);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_node_val_get_label_val(badValue, &labelVal), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, NodeValGetID) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));

    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value nodeVal;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &nodeVal), KoreDBSuccess);
    koredb_value nodeIDVal;
    ASSERT_EQ(koredb_node_val_get_id_val(&nodeVal, &nodeIDVal), KoreDBSuccess);
    ASSERT_NE(nodeIDVal._value, nullptr);
    koredb_internal_id_t internalID;
    ASSERT_EQ(koredb_value_get_internal_id(&nodeIDVal, &internalID), KoreDBSuccess);
    ASSERT_EQ(internalID.table_id, 0);
    ASSERT_EQ(internalID.offset, 0);
    koredb_value_destroy(&nodeIDVal);
    koredb_value_destroy(&nodeVal);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_node_val_get_id_val(badValue, &nodeIDVal), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, NodeValGetLabelName) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));

    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value nodeVal;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &nodeVal), KoreDBSuccess);
    koredb_value labelVal;
    ASSERT_EQ(koredb_node_val_get_label_val(&nodeVal, &labelVal), KoreDBSuccess);
    char* labelStr;
    ASSERT_EQ(koredb_value_get_string(&labelVal, &labelStr), KoreDBSuccess);
    ASSERT_STREQ(labelStr, "person");
    koredb_destroy_string(labelStr);
    koredb_value_destroy(&labelVal);
    koredb_value_destroy(&nodeVal);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_node_val_get_label_val(badValue, &labelVal), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, NodeValGetProperty) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection, (char*)"MATCH (a:person) RETURN a ORDER BY a.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value node;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &node), KoreDBSuccess);
    char* propertyName;
    ASSERT_EQ(koredb_node_val_get_property_name_at(&node, 0, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "ID");
    koredb_destroy_string(propertyName);
    ASSERT_EQ(koredb_node_val_get_property_name_at(&node, 1, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "fName");
    koredb_destroy_string(propertyName);
    ASSERT_EQ(koredb_node_val_get_property_name_at(&node, 2, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "gender");
    koredb_destroy_string(propertyName);
    ASSERT_EQ(koredb_node_val_get_property_name_at(&node, 3, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "isStudent");
    koredb_destroy_string(propertyName);

    koredb_value propertyValue;
    ASSERT_EQ(koredb_node_val_get_property_value_at(&node, 0, &propertyValue), KoreDBSuccess);
    int64_t propertyValueID;
    ASSERT_EQ(koredb_value_get_int64(&propertyValue, &propertyValueID), KoreDBSuccess);
    ASSERT_EQ(propertyValueID, 0);
    ASSERT_EQ(koredb_node_val_get_property_value_at(&node, 1, &propertyValue), KoreDBSuccess);
    char* propertyValuefName;
    ASSERT_EQ(koredb_value_get_string(&propertyValue, &propertyValuefName), KoreDBSuccess);
    ASSERT_STREQ(propertyValuefName, "Alice");
    koredb_destroy_string(propertyValuefName);
    ASSERT_EQ(koredb_node_val_get_property_value_at(&node, 2, &propertyValue), KoreDBSuccess);
    int64_t propertyValueGender;
    ASSERT_EQ(koredb_value_get_int64(&propertyValue, &propertyValueGender), KoreDBSuccess);
    ASSERT_EQ(propertyValueGender, 1);
    ASSERT_EQ(koredb_node_val_get_property_value_at(&node, 3, &propertyValue), KoreDBSuccess);
    bool propertyValueIsStudent;
    ASSERT_EQ(koredb_value_get_bool(&propertyValue, &propertyValueIsStudent), KoreDBSuccess);
    ASSERT_EQ(propertyValueIsStudent, true);
    koredb_value_destroy(&propertyValue);

    koredb_value_destroy(&node);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_node_val_get_property_name_at(badValue, 0, &propertyName), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, NodeValToString) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (b:organisation) RETURN b ORDER BY b.ID", &result);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value node;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &node), KoreDBSuccess);
    ASSERT_TRUE(node._is_owned_by_cpp);

    char* str = koredb_value_to_string(&node);
    ASSERT_STREQ(str,
        "{_ID: 1:0, _LABEL: organisation, ID: 1, name: ABFsUni, orgCode: 325, mark: 3.700000, "
        "score: -2, history: 10 years 5 months 13 hours 24 us, licenseValidInterval: 3 years "
        "5 days, rating: 1.000000, state: {revenue: 138, location: ['toronto','montr,eal'], "
        "stock: {price: [96,56], volume: 1000}}, info: 3.120000}");
    koredb_destroy_string(str);

    koredb_value_destroy(&node);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);
}

TEST_F(CApiValueTest, RelValGetProperty) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[e:workAt]-> (b:organisation) RETURN e ORDER BY a.ID, b.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value rel;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &rel), KoreDBSuccess);
    ASSERT_TRUE(rel._is_owned_by_cpp);
    uint64_t propertiesSize;
    ASSERT_EQ(koredb_rel_val_get_property_size(&rel, &propertiesSize), KoreDBSuccess);
    ASSERT_EQ(propertiesSize, 3);

    char* propertyName;
    ASSERT_EQ(koredb_rel_val_get_property_name_at(&rel, 0, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "year");
    koredb_destroy_string(propertyName);

    ASSERT_EQ(koredb_rel_val_get_property_name_at(&rel, 1, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "grading");
    koredb_destroy_string(propertyName);
    ASSERT_EQ(koredb_rel_val_get_property_name_at(&rel, 2, &propertyName), KoreDBSuccess);
    ASSERT_STREQ(propertyName, "rating");
    koredb_destroy_string(propertyName);

    koredb_value propertyValue;
    ASSERT_EQ(koredb_rel_val_get_property_value_at(&rel, 0, &propertyValue), KoreDBSuccess);
    int64_t propertyValueYear;
    ASSERT_EQ(koredb_value_get_int64(&propertyValue, &propertyValueYear), KoreDBSuccess);
    ASSERT_EQ(propertyValueYear, 2015);

    ASSERT_EQ(koredb_rel_val_get_property_value_at(&rel, 1, &propertyValue), KoreDBSuccess);
    koredb_value listValue;
    ASSERT_EQ(koredb_value_get_list_element(&propertyValue, 0, &listValue), KoreDBSuccess);
    double listValueGrading;
    ASSERT_EQ(koredb_value_get_double(&listValue, &listValueGrading), KoreDBSuccess);
    ASSERT_DOUBLE_EQ(listValueGrading, 3.8);
    ASSERT_EQ(koredb_value_get_list_element(&propertyValue, 1, &listValue), KoreDBSuccess);
    ASSERT_EQ(koredb_value_get_double(&listValue, &listValueGrading), KoreDBSuccess);
    ASSERT_DOUBLE_EQ(listValueGrading, 2.5);
    koredb_value_destroy(&listValue);

    ASSERT_EQ(koredb_rel_val_get_property_value_at(&rel, 2, &propertyValue), KoreDBSuccess);
    float propertyValueRating;
    ASSERT_EQ(koredb_value_get_float(&propertyValue, &propertyValueRating), KoreDBSuccess);
    ASSERT_FLOAT_EQ(propertyValueRating, 8.2);
    koredb_value_destroy(&propertyValue);

    koredb_value_destroy(&rel);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_rel_val_get_property_name_at(badValue, 0, &propertyName), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, RelValToString) {
    koredb_query_result result;
    koredb_flat_tuple flatTuple;
    koredb_state state;
    auto connection = getConnection();
    state = koredb_connection_query(connection,
        (char*)"MATCH (a:person) -[e:workAt]-> (b:organisation) RETURN e ORDER BY a.ID, b.ID",
        &result);
    ASSERT_EQ(state, KoreDBSuccess);
    ASSERT_TRUE(koredb_query_result_is_success(&result));
    ASSERT_TRUE(koredb_query_result_has_next(&result));
    state = koredb_query_result_get_next(&result, &flatTuple);
    ASSERT_EQ(state, KoreDBSuccess);
    koredb_value rel;
    ASSERT_EQ(koredb_flat_tuple_get_value(&flatTuple, 0, &rel), KoreDBSuccess);
    ASSERT_TRUE(rel._is_owned_by_cpp);
    char* str;
    ASSERT_EQ(koredb_rel_val_to_string(&rel, &str), KoreDBSuccess);
    ASSERT_STREQ(str, "(0:2)-{_LABEL: workAt, _ID: 7:0, year: 2015, grading: [3.800000,2.500000], "
                      "rating: 8.200000}->(1:1)");
    koredb_destroy_string(str);
    koredb_value_destroy(&rel);
    koredb_flat_tuple_destroy(&flatTuple);
    koredb_query_result_destroy(&result);

    koredb_value* badValue = koredb_value_create_string((char*)"abcdefg");
    ASSERT_EQ(koredb_rel_val_to_string(badValue, &str), KoreDBError);
    koredb_value_destroy(badValue);
}

TEST_F(CApiValueTest, GetTmFromNonStandardTimestamp) {
    koredb_timestamp_ns_t timestamp_ns = koredb_timestamp_ns_t{17515323532900000};
    koredb_timestamp_ms_t timestamp_ms = koredb_timestamp_ms_t{1012323435341};
    koredb_timestamp_sec_t timestamp_sec = koredb_timestamp_sec_t{1432135648};
    koredb_timestamp_tz_t timestamp_tz = koredb_timestamp_tz_t{771513532900000};
    struct tm tm;
    ASSERT_EQ(koredb_timestamp_ns_to_tm(timestamp_ns, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 70);
    ASSERT_EQ(tm.tm_mon, 6);
    ASSERT_EQ(tm.tm_mday, 22);
    ASSERT_EQ(tm.tm_hour, 17);
    ASSERT_EQ(tm.tm_min, 22);
    ASSERT_EQ(tm.tm_sec, 3);
    ASSERT_EQ(koredb_timestamp_ms_to_tm(timestamp_ms, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 102);
    ASSERT_EQ(tm.tm_mon, 0);
    ASSERT_EQ(tm.tm_mday, 29);
    ASSERT_EQ(tm.tm_hour, 16);
    ASSERT_EQ(tm.tm_min, 57);
    ASSERT_EQ(tm.tm_sec, 15);
    ASSERT_EQ(koredb_timestamp_sec_to_tm(timestamp_sec, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 115);
    ASSERT_EQ(tm.tm_mon, 4);
    ASSERT_EQ(tm.tm_mday, 20);
    ASSERT_EQ(tm.tm_hour, 15);
    ASSERT_EQ(tm.tm_min, 27);
    ASSERT_EQ(tm.tm_sec, 28);
    ASSERT_EQ(koredb_timestamp_tz_to_tm(timestamp_tz, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 94);
    ASSERT_EQ(tm.tm_mon, 5);
    ASSERT_EQ(tm.tm_mday, 13);
    ASSERT_EQ(tm.tm_hour, 13);
    ASSERT_EQ(tm.tm_min, 18);
    ASSERT_EQ(tm.tm_sec, 52);
}

TEST_F(CApiValueTest, GetTmFromTimestamp) {
    koredb_timestamp_t timestamp = koredb_timestamp_t{171513532900000};
    struct tm tm;
    ASSERT_EQ(koredb_timestamp_to_tm(timestamp, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 75);
    ASSERT_EQ(tm.tm_mon, 5);
    ASSERT_EQ(tm.tm_mday, 9);
    ASSERT_EQ(tm.tm_hour, 2);
    ASSERT_EQ(tm.tm_min, 38);
    ASSERT_EQ(tm.tm_sec, 52);
}

TEST_F(CApiValueTest, GetTmFromDate) {
    koredb_date_t date = koredb_date_t{-255};
    struct tm tm;
    ASSERT_EQ(koredb_date_to_tm(date, &tm), KoreDBSuccess);
    ASSERT_EQ(tm.tm_year, 69);
    ASSERT_EQ(tm.tm_mon, 3);
    ASSERT_EQ(tm.tm_mday, 21);
    ASSERT_EQ(tm.tm_hour, 0);
    ASSERT_EQ(tm.tm_min, 0);
    ASSERT_EQ(tm.tm_sec, 0);
}

TEST_F(CApiValueTest, GetTimestampFromTm) {
    struct tm tm;
    tm.tm_year = 75;
    tm.tm_mon = 5;
    tm.tm_mday = 9;
    tm.tm_hour = 2;
    tm.tm_min = 38;
    tm.tm_sec = 52;
    koredb_timestamp_t timestamp;
    ASSERT_EQ(koredb_timestamp_from_tm(tm, &timestamp), KoreDBSuccess);
    ASSERT_EQ(timestamp.value, 171513532000000);
}

TEST_F(CApiValueTest, GetNonStandardTimestampFromTm) {
    struct tm tm;
    tm.tm_year = 70;
    tm.tm_mon = 6;
    tm.tm_mday = 22;
    tm.tm_hour = 17;
    tm.tm_min = 22;
    tm.tm_sec = 3;
    koredb_timestamp_ns_t timestamp_ns;
    ASSERT_EQ(koredb_timestamp_ns_from_tm(tm, &timestamp_ns), KoreDBSuccess);
    ASSERT_EQ(timestamp_ns.value, 17515323000000000);
    tm.tm_year = 102;
    tm.tm_mon = 0;
    tm.tm_mday = 29;
    tm.tm_hour = 16;
    tm.tm_min = 57;
    tm.tm_sec = 15;
    koredb_timestamp_ms_t timestamp_ms;
    ASSERT_EQ(koredb_timestamp_ms_from_tm(tm, &timestamp_ms), KoreDBSuccess);
    ASSERT_EQ(timestamp_ms.value, 1012323435000);
    tm.tm_year = 115;
    tm.tm_mon = 4;
    tm.tm_mday = 20;
    tm.tm_hour = 15;
    tm.tm_min = 27;
    tm.tm_sec = 28;
    koredb_timestamp_sec_t timestamp_sec;
    ASSERT_EQ(koredb_timestamp_sec_from_tm(tm, &timestamp_sec), KoreDBSuccess);
    ASSERT_EQ(timestamp_sec.value, 1432135648);
    tm.tm_year = 94;
    tm.tm_mon = 5;
    tm.tm_mday = 13;
    tm.tm_hour = 13;
    tm.tm_min = 18;
    tm.tm_sec = 52;
    koredb_timestamp_tz_t timestamp_tz;
    ASSERT_EQ(koredb_timestamp_tz_from_tm(tm, &timestamp_tz), KoreDBSuccess);
    ASSERT_EQ(timestamp_tz.value, 771513532000000);
}

TEST_F(CApiValueTest, GetDateFromTm) {
    struct tm tm;
    tm.tm_year = 69;
    tm.tm_mon = 3;
    tm.tm_mday = 21;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    koredb_date_t date;
    ASSERT_EQ(koredb_date_from_tm(tm, &date), KoreDBSuccess);
    ASSERT_EQ(date.days, -255);
}

TEST_F(CApiValueTest, GetDateFromString) {
    char input[] = "1969-04-21";
    koredb_date_t date;
    ASSERT_EQ(koredb_date_from_string(input, &date), KoreDBSuccess);
    ASSERT_EQ(date.days, -255);

    char badInput[] = "this is not a date";
    ASSERT_EQ(koredb_date_from_string(badInput, &date), KoreDBError);
}

TEST_F(CApiValueTest, GetStringFromDate) {
    koredb_date_t date = koredb_date_t{-255};
    char* str;
    ASSERT_EQ(koredb_date_to_string(date, &str), KoreDBSuccess);
    ASSERT_STREQ(str, "1969-04-21");
    koredb_destroy_string(str);
}

TEST_F(CApiValueTest, GetDifftimeFromInterval) {
    koredb_interval_t interval = koredb_interval_t{36, 2, 46920000000};
    double difftime;
    koredb_interval_to_difftime(interval, &difftime);
    ASSERT_DOUBLE_EQ(difftime, 93531720);
}

TEST_F(CApiValueTest, GetIntervalFromDifftime) {
    double difftime = 211110160.479;
    koredb_interval_t interval;
    koredb_interval_from_difftime(difftime, &interval);
    ASSERT_EQ(interval.months, 81);
    ASSERT_EQ(interval.days, 13);
    ASSERT_EQ(interval.micros, 34960479000);
}
