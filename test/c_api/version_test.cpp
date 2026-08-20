#include <filesystem>
#include <fstream>

#include "c_api/koredb.h"
#include "c_api_test/c_api_test.h"
#include "gtest/gtest.h"

using namespace koredb::main;
using namespace koredb::testing;
using namespace koredb::common;

class CApiVersionTest : public CApiTest {
public:
    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }

    void TearDown() override { APIDBTest::TearDown(); }
};

class EmptyCApiVersionTest : public CApiVersionTest {
public:
    std::string getInputDir() override { return "empty"; }
};

TEST_F(EmptyCApiVersionTest, GetVersion) {
    koredb_connection_destroy(&connection);
    koredb_database_destroy(&_database);
    auto version = koredb_get_version();
    ASSERT_NE(version, nullptr);
    ASSERT_STREQ(version, KOREDB_CMAKE_VERSION);
    koredb_destroy_string(version);
}

TEST_F(CApiVersionTest, GetStorageVersion) {
    auto storageVersion = koredb_get_storage_version();
    if (inMemMode) {
        GTEST_SKIP();
    }
    // Reset the database to ensure that the lock on db file is released.
    koredb_connection_destroy(&connection);
    koredb_database_destroy(&_database);
    auto data = std::filesystem::path(databasePath);
    std::ifstream dbFile;
    dbFile.open(data, std::ios::binary);
    ASSERT_TRUE(dbFile.is_open());
    char magic[5];
    dbFile.read(magic, 4);
    magic[4] = '\0';
    ASSERT_STREQ(magic, "KUZU");
    uint64_t actualVersion;
    dbFile.read(reinterpret_cast<char*>(&actualVersion), sizeof(actualVersion));
    dbFile.close();
    ASSERT_EQ(storageVersion, actualVersion);
}

TEST_F(EmptyCApiVersionTest, GetStorageVersion) {
    auto storageVersion = koredb_get_storage_version();
    if (inMemMode) {
        GTEST_SKIP();
    }
    // Reset the database to ensure that the lock on db file is released.
    koredb_connection_destroy(&connection);
    koredb_database_destroy(&_database);
    auto data = std::filesystem::path(databasePath);
    std::ifstream dbFile;
    dbFile.open(data, std::ios::binary);
    ASSERT_TRUE(dbFile.is_open());
    char magic[5];
    dbFile.read(magic, 4);
    magic[4] = '\0';
    ASSERT_STREQ(magic, "KUZU");
    uint64_t actualVersion;
    dbFile.read(reinterpret_cast<char*>(&actualVersion), sizeof(actualVersion));
    dbFile.close();
    ASSERT_EQ(storageVersion, actualVersion);
}
