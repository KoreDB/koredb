#include "c_api/koredb.h"
#include "common/types/types.h"
#include "gtest/gtest.h"

using namespace koredb::common;

TEST(CApiDataTypeTest, Create) {
    koredb_logical_type dataType;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &dataType);
    ASSERT_NE(dataType._data_type, nullptr);
    auto dataTypeCpp = (LogicalType*)dataType._data_type;
    ASSERT_EQ(dataTypeCpp->getLogicalTypeID(), LogicalTypeID::INT64);

    koredb_logical_type dataType2;
    koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, &dataType, 0, &dataType2);
    ASSERT_NE(dataType2._data_type, nullptr);
    auto dataTypeCpp2 = (LogicalType*)dataType2._data_type;
    ASSERT_EQ(dataTypeCpp2->getLogicalTypeID(), LogicalTypeID::LIST);
    // ASSERT_EQ(dataTypeCpp2->getChildType()->getLogicalTypeID(), LogicalTypeID::INT64);

    koredb_logical_type dataType3;
    koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, &dataType, 100, &dataType3);
    ASSERT_NE(dataType3._data_type, nullptr);
    auto dataTypeCpp3 = (LogicalType*)dataType3._data_type;
    ASSERT_EQ(dataTypeCpp3->getLogicalTypeID(), LogicalTypeID::ARRAY);
    // ASSERT_EQ(dataTypeCpp3->getChildType()->getLogicalTypeID(), LogicalTypeID::INT64);
    ASSERT_EQ(ArrayType::getNumElements(*dataTypeCpp3), 100);

    // Since child type is copied, we should be able to destroy the original type without an error.
    koredb_data_type_destroy(&dataType);
    koredb_data_type_destroy(&dataType2);
    koredb_data_type_destroy(&dataType3);
}

TEST(CApiDataTypeTest, Clone) {
    koredb_logical_type dataType;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &dataType);
    ASSERT_NE(dataType._data_type, nullptr);
    koredb_logical_type dataTypeClone;
    koredb_data_type_clone(&dataType, &dataTypeClone);
    ASSERT_NE(dataTypeClone._data_type, nullptr);
    auto dataTypeCpp = (LogicalType*)dataType._data_type;
    auto dataTypeCloneCpp = (LogicalType*)dataTypeClone._data_type;
    ASSERT_TRUE(*dataTypeCpp == *dataTypeCloneCpp);

    koredb_logical_type dataType2;
    koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, &dataType, 0, &dataType2);
    ASSERT_NE(dataType2._data_type, nullptr);
    koredb_logical_type dataTypeClone2;
    koredb_data_type_clone(&dataType2, &dataTypeClone2);
    ASSERT_NE(dataTypeClone2._data_type, nullptr);
    auto dataTypeCpp2 = (LogicalType*)dataType2._data_type;
    auto dataTypeCloneCpp2 = (LogicalType*)dataTypeClone2._data_type;
    ASSERT_TRUE(*dataTypeCpp2 == *dataTypeCloneCpp2);

    koredb_logical_type dataType3;
    koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, &dataType, 100, &dataType3);
    ASSERT_NE(dataType3._data_type, nullptr);
    koredb_logical_type dataTypeClone3;
    koredb_data_type_clone(&dataType3, &dataTypeClone3);
    ASSERT_NE(dataTypeClone3._data_type, nullptr);
    auto dataTypeCpp3 = (LogicalType*)dataType3._data_type;
    auto dataTypeCloneCpp3 = (LogicalType*)dataTypeClone3._data_type;
    ASSERT_TRUE(*dataTypeCpp3 == *dataTypeCloneCpp3);

    koredb_data_type_destroy(&dataType);
    koredb_data_type_destroy(&dataType2);
    koredb_data_type_destroy(&dataType3);
    koredb_data_type_destroy(&dataTypeClone);
    koredb_data_type_destroy(&dataTypeClone2);
    koredb_data_type_destroy(&dataTypeClone3);
}

TEST(CApiDataTypeTest, Equals) {
    koredb_logical_type dataType;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &dataType);
    ASSERT_NE(dataType._data_type, nullptr);
    koredb_logical_type dataTypeClone;
    koredb_data_type_clone(&dataType, &dataTypeClone);
    ASSERT_NE(dataTypeClone._data_type, nullptr);
    ASSERT_TRUE(koredb_data_type_equals(&dataType, &dataTypeClone));

    koredb_logical_type dataType2;
    koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, &dataType, 0, &dataType2);
    ASSERT_NE(dataType2._data_type, nullptr);
    koredb_logical_type dataTypeClone2;
    koredb_data_type_clone(&dataType2, &dataTypeClone2);
    ASSERT_NE(dataTypeClone2._data_type, nullptr);
    ASSERT_TRUE(koredb_data_type_equals(&dataType2, &dataTypeClone2));

    koredb_logical_type dataType3;
    koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, &dataType, 100, &dataType3);
    ASSERT_NE(dataType3._data_type, nullptr);
    koredb_logical_type dataTypeClone3;
    koredb_data_type_clone(&dataType3, &dataTypeClone3);
    ASSERT_NE(dataTypeClone3._data_type, nullptr);
    ASSERT_TRUE(koredb_data_type_equals(&dataType3, &dataTypeClone3));

    ASSERT_FALSE(koredb_data_type_equals(&dataType, &dataType2));
    ASSERT_FALSE(koredb_data_type_equals(&dataType, &dataType3));
    ASSERT_FALSE(koredb_data_type_equals(&dataType2, &dataType3));

    koredb_data_type_destroy(&dataType);
    koredb_data_type_destroy(&dataType2);
    koredb_data_type_destroy(&dataType3);
    koredb_data_type_destroy(&dataTypeClone);
    koredb_data_type_destroy(&dataTypeClone2);
    koredb_data_type_destroy(&dataTypeClone3);
}

TEST(CApiDataTypeTest, GetID) {
    koredb_logical_type dataType;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &dataType);
    ASSERT_NE(dataType._data_type, nullptr);
    ASSERT_EQ(koredb_data_type_get_id(&dataType), koredb_data_type_id::KOREDB_INT64);

    koredb_logical_type dataType2;
    koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, &dataType, 0, &dataType2);
    ASSERT_NE(dataType2._data_type, nullptr);
    ASSERT_EQ(koredb_data_type_get_id(&dataType2), koredb_data_type_id::KOREDB_LIST);

    koredb_logical_type dataType3;
    koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, &dataType, 100, &dataType3);
    ASSERT_NE(dataType3._data_type, nullptr);
    ASSERT_EQ(koredb_data_type_get_id(&dataType3), koredb_data_type_id::KOREDB_ARRAY);

    koredb_data_type_destroy(&dataType);
    koredb_data_type_destroy(&dataType2);
    koredb_data_type_destroy(&dataType3);
}

// TODO(Chang): The getChildType interface has been removed from the C++ DataType class.
// Consider adding the StructType/ListType helper to C binding.
// TEST(CApiDataTypeTest, GetChildType) {
//    auto dataType = koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0);
//    ASSERT_NE(dataType, nullptr);
//    ASSERT_EQ(koredb_data_type_get_child_type(dataType), nullptr);
//
//    auto dataType2 = koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, dataType, 0);
//    ASSERT_NE(dataType2, nullptr);
//    auto childType2 = koredb_data_type_get_child_type(dataType2);
//    ASSERT_NE(childType2, nullptr);
//    ASSERT_EQ(koredb_data_type_get_id(childType2), koredb_data_type_id::KOREDB_INT64);
//    koredb_data_type_destroy(childType2);
//    koredb_data_type_destroy(dataType2);
//
//    auto dataType3 = koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, dataType, 100);
//    ASSERT_NE(dataType3, nullptr);
//    auto childType3 = koredb_data_type_get_child_type(dataType3);
//    koredb_data_type_destroy(dataType3);
//    // Destroying dataType3 should not destroy childType3.
//    ASSERT_NE(childType3, nullptr);
//    ASSERT_EQ(koredb_data_type_get_id(childType3), koredb_data_type_id::KOREDB_INT64);
//    koredb_data_type_destroy(childType3);
//
//    koredb_data_type_destroy(dataType);
//}

TEST(CApiDataTypeTest, GetFixedNumElementsInList) {
    koredb_logical_type dataType;
    koredb_data_type_create(koredb_data_type_id::KOREDB_INT64, nullptr, 0, &dataType);
    ASSERT_NE(dataType._data_type, nullptr);
    uint64_t numElements;
    ASSERT_EQ(koredb_data_type_get_num_elements_in_array(&dataType, &numElements), KoreDBError);

    koredb_logical_type dataType2;
    koredb_data_type_create(koredb_data_type_id::KOREDB_LIST, &dataType, 0, &dataType2);
    ASSERT_NE(dataType2._data_type, nullptr);
    ASSERT_EQ(koredb_data_type_get_num_elements_in_array(&dataType2, &numElements), KoreDBError);

    koredb_logical_type dataType3;
    koredb_data_type_create(koredb_data_type_id::KOREDB_ARRAY, &dataType, 100, &dataType3);
    ASSERT_NE(dataType3._data_type, nullptr);
    ASSERT_EQ(koredb_data_type_get_num_elements_in_array(&dataType3, &numElements), KoreDBSuccess);
    ASSERT_EQ(numElements, 100);

    koredb_data_type_destroy(&dataType);
    koredb_data_type_destroy(&dataType2);
    koredb_data_type_destroy(&dataType3);
}
