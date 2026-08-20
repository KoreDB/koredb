#include "c_api/koredb.h"
#include "common/exception/exception.h"
#include "main/koredb.h"

namespace koredb {
namespace common {
class Value;
}
} // namespace koredb

using namespace koredb::common;
using namespace koredb::main;

koredb_state koredb_connection_init(koredb_database* database, koredb_connection* out_connection) {
    if (database == nullptr || database->_database == nullptr) {
        out_connection->_connection = nullptr;
        return KoreDBError;
    }
    try {
        out_connection->_connection = new Connection(static_cast<Database*>(database->_database));
    } catch (Exception& e) {
        out_connection->_connection = nullptr;
        return KoreDBError;
    }
    return KoreDBSuccess;
}

void koredb_connection_destroy(koredb_connection* connection) {
    if (connection == nullptr) {
        return;
    }
    if (connection->_connection != nullptr) {
        delete static_cast<Connection*>(connection->_connection);
    }
}

koredb_state koredb_connection_set_max_num_thread_for_exec(koredb_connection* connection,
    uint64_t num_threads) {
    if (connection == nullptr || connection->_connection == nullptr) {
        return KoreDBError;
    }
    try {
        static_cast<Connection*>(connection->_connection)->setMaxNumThreadForExec(num_threads);
    } catch (Exception& e) {
        return KoreDBError;
    }
    return KoreDBSuccess;
}

koredb_state koredb_connection_get_max_num_thread_for_exec(koredb_connection* connection,
    uint64_t* out_result) {
    if (connection == nullptr || connection->_connection == nullptr) {
        return KoreDBError;
    }
    try {
        *out_result = static_cast<Connection*>(connection->_connection)->getMaxNumThreadForExec();
    } catch (Exception& e) {
        return KoreDBError;
    }
    return KoreDBSuccess;
}

koredb_state koredb_connection_query(koredb_connection* connection, const char* query,
    koredb_query_result* out_query_result) {
    if (connection == nullptr || connection->_connection == nullptr) {
        return KoreDBError;
    }
    try {
        auto query_result =
            static_cast<Connection*>(connection->_connection)->query(query).release();
        if (query_result == nullptr) {
            return KoreDBError;
        }
        out_query_result->_query_result = query_result;
        out_query_result->_is_owned_by_cpp = false;
        if (!query_result->isSuccess()) {
            return KoreDBError;
        }
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_connection_prepare(koredb_connection* connection, const char* query,
    koredb_prepared_statement* out_prepared_statement) {
    if (connection == nullptr || connection->_connection == nullptr) {
        return KoreDBError;
    }
    try {
        auto prepared_statement =
            static_cast<Connection*>(connection->_connection)->prepare(query).release();
        if (prepared_statement == nullptr) {
            return KoreDBError;
        }
        out_prepared_statement->_prepared_statement = prepared_statement;
        out_prepared_statement->_bound_values =
            new std::unordered_map<std::string, std::unique_ptr<Value>>;
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
    return KoreDBSuccess;
}

koredb_state koredb_connection_execute(koredb_connection* connection,
    koredb_prepared_statement* prepared_statement, koredb_query_result* out_query_result) {
    if (connection == nullptr || connection->_connection == nullptr ||
        prepared_statement == nullptr || prepared_statement->_prepared_statement == nullptr ||
        prepared_statement->_bound_values == nullptr) {
        return KoreDBError;
    }
    try {
        auto prepared_statement_ptr =
            static_cast<PreparedStatement*>(prepared_statement->_prepared_statement);
        auto bound_values = static_cast<std::unordered_map<std::string, std::unique_ptr<Value>>*>(
            prepared_statement->_bound_values);

        // Must copy the parameters for safety, and so that the parameters in the prepared statement
        // stay the same.
        std::unordered_map<std::string, std::unique_ptr<Value>> copied_bound_values;
        for (auto& [name, value] : *bound_values) {
            copied_bound_values.emplace(name, value->copy());
        }

        auto query_result =
            static_cast<Connection*>(connection->_connection)
                ->executeWithParams(prepared_statement_ptr, std::move(copied_bound_values))
                .release();
        if (query_result == nullptr) {
            return KoreDBError;
        }
        out_query_result->_query_result = query_result;
        out_query_result->_is_owned_by_cpp = false;
        if (!query_result->isSuccess()) {
            return KoreDBError;
        }
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}
void koredb_connection_interrupt(koredb_connection* connection) {
    static_cast<Connection*>(connection->_connection)->interrupt();
}

koredb_state koredb_connection_set_query_timeout(koredb_connection* connection,
    uint64_t timeout_in_ms) {
    if (connection == nullptr || connection->_connection == nullptr) {
        return KoreDBError;
    }
    try {
        static_cast<Connection*>(connection->_connection)->setQueryTimeOut(timeout_in_ms);
    } catch (Exception& e) {
        return KoreDBError;
    }
    return KoreDBSuccess;
}
