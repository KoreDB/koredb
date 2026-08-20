#pragma once

#include "graph_test/base_graph_test.h"

namespace koredb {
namespace testing {

class ApiTest : public BaseGraphTest {
public:
    void SetUp() override {
        BaseGraphTest::SetUp();
        createDBAndConn();
        initGraph();
    }

    std::string getInputDir() override {
        return TestHelper::appendKoreDBRootPath("dataset/tinysnb/");
    }
};

} // namespace testing
} // namespace koredb
