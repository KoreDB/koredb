#include "main/prepared_statement.h"

#include "c_api/helpers.h"
#include "c_api/koredb.h"
#include "common/types/value/value.h"

using namespace koredb::common;
using namespace koredb::main;

void koredb_prepared_statement_bind_cpp_value(koredb_prepared_statement* prepared_statement,
    const char* param_name, std::unique_ptr<Value> value) {
    auto* bound_values = static_cast<std::unordered_map<std::string, std::unique_ptr<Value>>*>(
        prepared_statement->_bound_values);
    bound_values->erase(param_name);
    bound_values->insert({param_name, std::move(value)});
}

void koredb_prepared_statement_destroy(koredb_prepared_statement* prepared_statement) {
    if (prepared_statement == nullptr) {
        return;
    }
    if (prepared_statement->_prepared_statement != nullptr) {
        delete static_cast<PreparedStatement*>(prepared_statement->_prepared_statement);
    }
    if (prepared_statement->_bound_values != nullptr) {
        delete static_cast<std::unordered_map<std::string, std::unique_ptr<Value>>*>(
            prepared_statement->_bound_values);
    }
}

bool koredb_prepared_statement_is_success(koredb_prepared_statement* prepared_statement) {
    return static_cast<PreparedStatement*>(prepared_statement->_prepared_statement)->isSuccess();
}

char* koredb_prepared_statement_get_error_message(koredb_prepared_statement* prepared_statement) {
    auto error_message =
        static_cast<PreparedStatement*>(prepared_statement->_prepared_statement)->getErrorMessage();
    if (error_message.empty()) {
        return nullptr;
    }
    return convertToOwnedCString(error_message);
}

koredb_state koredb_prepared_statement_bind_bool(koredb_prepared_statement* prepared_statement,
    const char* param_name, bool value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_int64(koredb_prepared_statement* prepared_statement,
    const char* param_name, int64_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_int32(koredb_prepared_statement* prepared_statement,
    const char* param_name, int32_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_int16(koredb_prepared_statement* prepared_statement,
    const char* param_name, int16_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_int8(koredb_prepared_statement* prepared_statement,
    const char* param_name, int8_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_uint64(koredb_prepared_statement* prepared_statement,
    const char* param_name, uint64_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_uint32(koredb_prepared_statement* prepared_statement,
    const char* param_name, uint32_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_uint16(koredb_prepared_statement* prepared_statement,
    const char* param_name, uint16_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_uint8(koredb_prepared_statement* prepared_statement,
    const char* param_name, uint8_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_double(koredb_prepared_statement* prepared_statement,
    const char* param_name, double value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_float(koredb_prepared_statement* prepared_statement,
    const char* param_name, float value) {
    try {
        auto value_ptr = std::make_unique<Value>(value);
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_date(koredb_prepared_statement* prepared_statement,
    const char* param_name, koredb_date_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(date_t(value.days));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_timestamp_ns(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_ns_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(timestamp_ns_t(value.value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_timestamp_ms(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_ms_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(timestamp_ms_t(value.value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_timestamp_sec(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_sec_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(timestamp_sec_t(value.value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_timestamp_tz(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_tz_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(timestamp_tz_t(value.value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_timestamp(koredb_prepared_statement* prepared_statement,
    const char* param_name, koredb_timestamp_t value) {
    try {
        auto value_ptr = std::make_unique<Value>(timestamp_t(value.value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_interval(koredb_prepared_statement* prepared_statement,
    const char* param_name, koredb_interval_t value) {
    try {
        auto value_ptr =
            std::make_unique<Value>(interval_t(value.months, value.days, value.micros));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_string(koredb_prepared_statement* prepared_statement,
    const char* param_name, const char* value) {
    try {
        auto value_ptr = std::make_unique<Value>(std::string(value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}

koredb_state koredb_prepared_statement_bind_value(koredb_prepared_statement* prepared_statement,
    const char* param_name, koredb_value* value) {
    try {
        auto value_ptr = std::make_unique<Value>(*static_cast<Value*>(value->_value));
        koredb_prepared_statement_bind_cpp_value(prepared_statement, param_name,
            std::move(value_ptr));
        return KoreDBSuccess;
    } catch (Exception& e) {
        return KoreDBError;
    }
}
