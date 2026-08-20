#pragma once

#include "main/koredb.h"
#include "pybind_include.h"

using koredb::common::LogicalType;
using koredb::common::Value;

struct NPArrayWrapper {

public:
    NPArrayWrapper(const LogicalType& type, uint64_t numFlatTuple);

    void appendElement(Value* value);

private:
    py::dtype convertToArrayType(const LogicalType& type);

public:
    py::array data;
    uint8_t* dataBuffer;
    py::array mask;
    LogicalType type;
    uint64_t numElements;
};

class QueryResultConverter {
public:
    explicit QueryResultConverter(koredb::main::QueryResult* queryResult);

    py::object toDF();

private:
    koredb::main::QueryResult* queryResult;
    std::vector<std::unique_ptr<NPArrayWrapper>> columns;
};
