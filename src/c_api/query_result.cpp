#include "main/query_result.h"

#include "c_api/helpers.h"
#include "c_api/koredb.h"

using namespace koredb::main;
using namespace koredb::common;
using namespace koredb::processor;

void koredb_query_result_destroy(koredb_query_result* query_result) {
    if (query_result == nullptr) {
        return;
    }
    if (query_result->_query_result != nullptr) {
        if (!query_result->_is_owned_by_cpp) {
            delete static_cast<QueryResult*>(query_result->_query_result);
        }
    }
}

bool koredb_query_result_is_success(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->isSuccess();
}

bool koredb_query_result_is_truncated(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->isTruncated();
}

char* koredb_query_result_get_truncation_reason(koredb_query_result* query_result) {
    auto reason = static_cast<QueryResult*>(query_result->_query_result)->getTruncationReason();
    if (reason.empty()) {
        return nullptr;
    }
    return convertToOwnedCString(reason);
}

char* koredb_query_result_get_error_message(koredb_query_result* query_result) {
    auto error_message = static_cast<QueryResult*>(query_result->_query_result)->getErrorMessage();
    if (error_message.empty()) {
        return nullptr;
    }
    return convertToOwnedCString(error_message);
}

uint64_t koredb_query_result_get_num_columns(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->getNumColumns();
}

koredb_state koredb_query_result_get_column_name(koredb_query_result* query_result, uint64_t index,
    char** out_column_name) {
    auto column_names = static_cast<QueryResult*>(query_result->_query_result)->getColumnNames();
    if (index >= column_names.size()) {
        return KoreDBError;
    }
    *out_column_name = convertToOwnedCString(column_names[index]);
    return KoreDBSuccess;
}

koredb_state koredb_query_result_get_column_data_type(koredb_query_result* query_result,
    uint64_t index, koredb_logical_type* out_column_data_type) {
    auto column_data_types =
        static_cast<QueryResult*>(query_result->_query_result)->getColumnDataTypes();
    if (index >= column_data_types.size()) {
        return KoreDBError;
    }
    const auto& column_data_type = column_data_types[index];
    out_column_data_type->_data_type = new LogicalType(column_data_type.copy());
    return KoreDBSuccess;
}

uint64_t koredb_query_result_get_num_tuples(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->getNumTuples();
}

koredb_state koredb_query_result_get_query_summary(koredb_query_result* query_result,
    koredb_query_summary* out_query_summary) {
    if (out_query_summary == nullptr) {
        return KoreDBError;
    }
    auto query_summary = static_cast<QueryResult*>(query_result->_query_result)->getQuerySummary();
    out_query_summary->_query_summary = query_summary;
    return KoreDBSuccess;
}

bool koredb_query_result_has_next(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->hasNext();
}

bool koredb_query_result_has_next_query_result(koredb_query_result* query_result) {
    return static_cast<QueryResult*>(query_result->_query_result)->hasNextQueryResult();
}

koredb_state koredb_query_result_get_next_query_result(koredb_query_result* query_result,
    koredb_query_result* out_query_result) {
    if (!koredb_query_result_has_next_query_result(query_result)) {
        return KoreDBError;
    }
    auto next_query_result =
        static_cast<QueryResult*>(query_result->_query_result)->getNextQueryResult();
    if (next_query_result == nullptr) {
        return KoreDBError;
    }
    out_query_result->_query_result = next_query_result;
    out_query_result->_is_owned_by_cpp = true;
    return KoreDBSuccess;
}

koredb_state koredb_query_result_get_next(koredb_query_result* query_result,
    koredb_flat_tuple* out_flat_tuple) {
    try {
        auto flat_tuple = static_cast<QueryResult*>(query_result->_query_result)->getNext();
        out_flat_tuple->_flat_tuple = flat_tuple.get();
        out_flat_tuple->_is_owned_by_cpp = true;
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

char* koredb_query_result_to_string(koredb_query_result* query_result) {
    std::string result_string = static_cast<QueryResult*>(query_result->_query_result)->toString();
    return convertToOwnedCString(result_string);
}

void koredb_query_result_reset_iterator(koredb_query_result* query_result) {
    static_cast<QueryResult*>(query_result->_query_result)->resetIterator();
}

koredb_state koredb_query_result_get_arrow_schema(koredb_query_result* query_result,
    ArrowSchema* out_schema) {
    try {
        *out_schema = *static_cast<QueryResult*>(query_result->_query_result)->getArrowSchema();
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_query_result_get_next_arrow_chunk(koredb_query_result* query_result,
    int64_t chunk_size, ArrowArray* out_arrow_array) {
    try {
        *out_arrow_array =
            *static_cast<QueryResult*>(query_result->_query_result)->getNextArrowChunk(chunk_size);
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}
