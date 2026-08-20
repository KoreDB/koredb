#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Export header from common/api.h */
// Helpers
#if defined _WIN32 || defined __CYGWIN__
#define KOREDB_HELPER_DLL_IMPORT __declspec(dllimport)
#define KOREDB_HELPER_DLL_EXPORT __declspec(dllexport)
#define KOREDB_HELPER_DLL_LOCAL
#define KOREDB_HELPER_DEPRECATED __declspec(deprecated)
#else
#define KOREDB_HELPER_DLL_IMPORT __attribute__((visibility("default")))
#define KOREDB_HELPER_DLL_EXPORT __attribute__((visibility("default")))
#define KOREDB_HELPER_DLL_LOCAL __attribute__((visibility("hidden")))
#define KOREDB_HELPER_DEPRECATED __attribute__((__deprecated__))
#endif

#ifdef KOREDB_STATIC_DEFINE
#define KOREDB_API
#define KOREDB_NO_EXPORT
#else
#ifndef KOREDB_API
#ifdef KOREDB_EXPORTS
/* We are building this library */
#define KOREDB_API KOREDB_HELPER_DLL_EXPORT
#else
/* We are using this library */
#define KOREDB_API KOREDB_HELPER_DLL_IMPORT
#endif
#endif

#endif

#ifndef KOREDB_DEPRECATED
#define KOREDB_DEPRECATED KOREDB_HELPER_DEPRECATED
#endif

#ifndef KOREDB_DEPRECATED_EXPORT
#define KOREDB_DEPRECATED_EXPORT KOREDB_API KOREDB_DEPRECATED
#endif
/* end export header */

// The Arrow C data interface.
// https://arrow.apache.org/docs/format/CDataInterface.html

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
    // Array type description
    const char* format;
    const char* name;
    const char* metadata;
    int64_t flags;
    int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;

    // Release callback
    void (*release)(struct ArrowSchema*);
    // Opaque producer-specific data
    void* private_data;
};

struct ArrowArray {
    // Array data description
    int64_t length;
    int64_t null_count;
    int64_t offset;
    int64_t n_buffers;
    int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;

    // Release callback
    void (*release)(struct ArrowArray*);
    // Opaque producer-specific data
    void* private_data;
};

#endif // ARROW_C_DATA_INTERFACE

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define KOREDB_C_API extern "C" KOREDB_API
#else
#define KOREDB_C_API KOREDB_API
#endif

/**
 * @brief Stores runtime configuration for creating or opening a Database
 */
typedef struct {
    // bufferPoolSize Max size of the buffer pool in bytes.
    // The larger the buffer pool, the more data from the database files is kept in memory,
    // reducing the amount of File I/O
    uint64_t buffer_pool_size;
    // The maximum number of threads to use during query execution
    uint64_t max_num_threads;
    // Whether or not to compress data on-disk for supported types
    bool enable_compression;
    // If true, open the database in read-only mode. No write transaction is allowed on the Database
    // object. If false, open the database read-write.
    bool read_only;
    //  The maximum size of the database in bytes. Note that this is introduced temporarily for now
    //  to get around with the default 8TB mmap address space limit under some environment. This
    //  will be removed once we implemente a better solution later. The value is default to 1 << 43
    //  (8TB) under 64-bit environment and 1GB under 32-bit one (see `DEFAULT_VM_REGION_MAX_SIZE`).
    uint64_t max_db_size;
    // If true, the database will automatically checkpoint when the size of
    // the WAL file exceeds the checkpoint threshold.
    bool auto_checkpoint;
    // The threshold of the WAL file size in bytes. When the size of the
    // WAL file exceeds this threshold, the database will checkpoint if auto_checkpoint is true.
    uint64_t checkpoint_threshold;

#if defined(__APPLE__)
    // The thread quality of service (QoS) for the worker threads.
    // This works for Swift bindings on Apple platforms only.
    uint32_t thread_qos;
#endif
} koredb_system_config;

/**
 * @brief koredb_database manages all database components.
 */
typedef struct {
    void* _database;
} koredb_database;

/**
 * @brief koredb_connection is used to interact with a Database instance. Each connection is
 * thread-safe. Multiple connections can connect to the same Database instance in a multi-threaded
 * environment.
 */
typedef struct {
    void* _connection;
} koredb_connection;

/**
 * @brief koredb_prepared_statement is a parameterized query which can avoid planning the same query
 * for repeated execution.
 */
typedef struct {
    void* _prepared_statement;
    void* _bound_values;
} koredb_prepared_statement;

/**
 * @brief koredb_query_result stores the result of a query.
 */
typedef struct {
    void* _query_result;
    bool _is_owned_by_cpp;
} koredb_query_result;

/**
 * @brief koredb_flat_tuple stores a vector of values.
 */
typedef struct {
    void* _flat_tuple;
    bool _is_owned_by_cpp;
} koredb_flat_tuple;

/**
 * @brief koredb_logical_type is the koredb internal representation of data types.
 */
typedef struct {
    void* _data_type;
} koredb_logical_type;

/**
 * @brief koredb_value is used to represent a value with any koredb internal dataType.
 */
typedef struct {
    void* _value;
    bool _is_owned_by_cpp;
} koredb_value;

/**
 * @brief koredb internal internal_id type which stores the table_id and offset of a node/rel.
 */
typedef struct {
    uint64_t table_id;
    uint64_t offset;
} koredb_internal_id_t;

/**
 * @brief koredb internal date type which stores the number of days since 1970-01-01 00:00:00 UTC.
 */
typedef struct {
    // Days since 1970-01-01 00:00:00 UTC.
    int32_t days;
} koredb_date_t;

/**
 * @brief koredb internal timestamp_ns type which stores the number of nanoseconds since 1970-01-01
 * 00:00:00 UTC.
 */
typedef struct {
    // Nanoseconds since 1970-01-01 00:00:00 UTC.
    int64_t value;
} koredb_timestamp_ns_t;

/**
 * @brief koredb internal timestamp_ms type which stores the number of milliseconds since 1970-01-01
 * 00:00:00 UTC.
 */
typedef struct {
    // Milliseconds since 1970-01-01 00:00:00 UTC.
    int64_t value;
} koredb_timestamp_ms_t;

/**
 * @brief koredb internal timestamp_sec_t type which stores the number of seconds since 1970-01-01
 * 00:00:00 UTC.
 */
typedef struct {
    // Seconds since 1970-01-01 00:00:00 UTC.
    int64_t value;
} koredb_timestamp_sec_t;

/**
 * @brief koredb internal timestamp_tz type which stores the number of microseconds since 1970-01-01
 * with timezone 00:00:00 UTC.
 */
typedef struct {
    // Microseconds since 1970-01-01 00:00:00 UTC.
    int64_t value;
} koredb_timestamp_tz_t;

/**
 * @brief koredb internal timestamp type which stores the number of microseconds since 1970-01-01
 * 00:00:00 UTC.
 */
typedef struct {
    // Microseconds since 1970-01-01 00:00:00 UTC.
    int64_t value;
} koredb_timestamp_t;

/**
 * @brief koredb internal interval type which stores the months, days and microseconds.
 */
typedef struct {
    int32_t months;
    int32_t days;
    int64_t micros;
} koredb_interval_t;

/**
 * @brief koredb_query_summary stores the execution time, plan, compiling time and query options of
 * a query.
 */
typedef struct {
    void* _query_summary;
} koredb_query_summary;

typedef struct {
    uint64_t low;
    int64_t high;
} koredb_int128_t;

/**
 * @brief enum class for koredb internal dataTypes.
 */
typedef enum {
    KOREDB_ANY = 0,
    KOREDB_NODE = 10,
    KOREDB_REL = 11,
    KOREDB_RECURSIVE_REL = 12,
    // SERIAL is a special data type that is used to represent a sequence of INT64 values that are
    // incremented by 1 starting from 0.
    KOREDB_SERIAL = 13,
    // fixed size types
    KOREDB_BOOL = 22,
    KOREDB_INT64 = 23,
    KOREDB_INT32 = 24,
    KOREDB_INT16 = 25,
    KOREDB_INT8 = 26,
    KOREDB_UINT64 = 27,
    KOREDB_UINT32 = 28,
    KOREDB_UINT16 = 29,
    KOREDB_UINT8 = 30,
    KOREDB_INT128 = 31,
    KOREDB_DOUBLE = 32,
    KOREDB_FLOAT = 33,
    KOREDB_DATE = 34,
    KOREDB_TIMESTAMP = 35,
    KOREDB_TIMESTAMP_SEC = 36,
    KOREDB_TIMESTAMP_MS = 37,
    KOREDB_TIMESTAMP_NS = 38,
    KOREDB_TIMESTAMP_TZ = 39,
    KOREDB_INTERVAL = 40,
    KOREDB_DECIMAL = 41,
    KOREDB_INTERNAL_ID = 42,
    // variable size types
    KOREDB_STRING = 50,
    KOREDB_BLOB = 51,
    KOREDB_LIST = 52,
    KOREDB_ARRAY = 53,
    KOREDB_STRUCT = 54,
    KOREDB_MAP = 55,
    KOREDB_UNION = 56,
    KOREDB_POINTER = 58,
    KOREDB_UUID = 59
} koredb_data_type_id;

/**
 * @brief enum class for koredb function return state.
 */
typedef enum { KoreDBSuccess = 0, KoreDBError = 1 } koredb_state;

// Database
/**
 * @brief Allocates memory and creates a koredb database instance at database_path with
 * bufferPoolSize=buffer_pool_size. Caller is responsible for calling koredb_database_destroy() to
 * release the allocated memory.
 * @param database_path The path to the database.
 * @param system_config The runtime configuration for creating or opening the database.
 * @param[out] out_database The output parameter that will hold the database instance.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_database_init(const char* database_path,
    koredb_system_config system_config, koredb_database* out_database);
/**
 * @brief Destroys the koredb database instance and frees the allocated memory.
 * @param database The database instance to destroy.
 */
KOREDB_C_API void koredb_database_destroy(koredb_database* database);

KOREDB_C_API koredb_system_config koredb_default_system_config();

// Connection
/**
 * @brief Allocates memory and creates a connection to the database. Caller is responsible for
 * calling koredb_connection_destroy() to release the allocated memory.
 * @param database The database instance to connect to.
 * @param[out] out_connection The output parameter that will hold the connection instance.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_init(koredb_database* database,
    koredb_connection* out_connection);
/**
 * @brief Destroys the connection instance and frees the allocated memory.
 * @param connection The connection instance to destroy.
 */
KOREDB_C_API void koredb_connection_destroy(koredb_connection* connection);
/**
 * @brief Sets the maximum number of threads to use for executing queries.
 * @param connection The connection instance to set max number of threads for execution.
 * @param num_threads The maximum number of threads to use for executing queries.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_set_max_num_thread_for_exec(
    koredb_connection* connection, uint64_t num_threads);

/**
 * @brief Returns the maximum number of threads of the connection to use for executing queries.
 * @param connection The connection instance to return max number of threads for execution.
 * @param[out] out_result The output parameter that will hold the maximum number of threads to use
 * for executing queries.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_get_max_num_thread_for_exec(
    koredb_connection* connection, uint64_t* out_result);
/**
 * @brief Executes the given query and returns the result.
 * @param connection The connection instance to execute the query.
 * @param query The query to execute.
 * @param[out] out_query_result The output parameter that will hold the result of the query.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_query(koredb_connection* connection, const char* query,
    koredb_query_result* out_query_result);
/**
 * @brief Prepares the given query and returns the prepared statement.
 * @param connection The connection instance to prepare the query.
 * @param query The query to prepare.
 * @param[out] out_prepared_statement The output parameter that will hold the prepared statement.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_prepare(koredb_connection* connection,
    const char* query, koredb_prepared_statement* out_prepared_statement);
/**
 * @brief Executes the prepared_statement using connection.
 * @param connection The connection instance to execute the prepared_statement.
 * @param prepared_statement The prepared statement to execute.
 * @param[out] out_query_result The output parameter that will hold the result of the query.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_execute(koredb_connection* connection,
    koredb_prepared_statement* prepared_statement, koredb_query_result* out_query_result);
/**
 * @brief Interrupts the current query execution in the connection.
 * @param connection The connection instance to interrupt.
 */
KOREDB_C_API void koredb_connection_interrupt(koredb_connection* connection);
/**
 * @brief Sets query timeout value in milliseconds for the connection.
 * @param connection The connection instance to set query timeout value.
 * @param timeout_in_ms The timeout value in milliseconds.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_connection_set_query_timeout(koredb_connection* connection,
    uint64_t timeout_in_ms);

// PreparedStatement
/**
 * @brief Destroys the prepared statement instance and frees the allocated memory.
 * @param prepared_statement The prepared statement instance to destroy.
 */
KOREDB_C_API void koredb_prepared_statement_destroy(koredb_prepared_statement* prepared_statement);
/**
 * @return the query is prepared successfully or not.
 */
KOREDB_C_API bool koredb_prepared_statement_is_success(
    koredb_prepared_statement* prepared_statement);
/**
 * @brief Returns the error message if the prepared statement is not prepared successfully.
 * The caller is responsible for freeing the returned string with `koredb_destroy_string`.
 * @param prepared_statement The prepared statement instance.
 * @return the error message if the statement is not prepared successfully or null
 * if the statement is prepared successfully.
 */
KOREDB_C_API char* koredb_prepared_statement_get_error_message(
    koredb_prepared_statement* prepared_statement);
/**
 * @brief Binds the given boolean value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The boolean value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_bool(
    koredb_prepared_statement* prepared_statement, const char* param_name, bool value);
/**
 * @brief Binds the given int64_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The int64_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_int64(
    koredb_prepared_statement* prepared_statement, const char* param_name, int64_t value);
/**
 * @brief Binds the given int32_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The int32_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_int32(
    koredb_prepared_statement* prepared_statement, const char* param_name, int32_t value);
/**
 * @brief Binds the given int16_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The int16_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_int16(
    koredb_prepared_statement* prepared_statement, const char* param_name, int16_t value);
/**
 * @brief Binds the given int8_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The int8_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_int8(
    koredb_prepared_statement* prepared_statement, const char* param_name, int8_t value);
/**
 * @brief Binds the given uint64_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The uint64_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_uint64(
    koredb_prepared_statement* prepared_statement, const char* param_name, uint64_t value);
/**
 * @brief Binds the given uint32_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The uint32_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_uint32(
    koredb_prepared_statement* prepared_statement, const char* param_name, uint32_t value);
/**
 * @brief Binds the given uint16_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The uint16_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_uint16(
    koredb_prepared_statement* prepared_statement, const char* param_name, uint16_t value);
/**
 * @brief Binds the given int8_t value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The int8_t value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_uint8(
    koredb_prepared_statement* prepared_statement, const char* param_name, uint8_t value);

/**
 * @brief Binds the given double value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The double value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_double(
    koredb_prepared_statement* prepared_statement, const char* param_name, double value);
/**
 * @brief Binds the given float value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The float value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_float(
    koredb_prepared_statement* prepared_statement, const char* param_name, float value);
/**
 * @brief Binds the given date value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The date value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_date(
    koredb_prepared_statement* prepared_statement, const char* param_name, koredb_date_t value);
/**
 * @brief Binds the given timestamp_ns value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The timestamp_ns value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_timestamp_ns(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_ns_t value);
/**
 * @brief Binds the given timestamp_sec value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The timestamp_sec value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_timestamp_sec(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_sec_t value);
/**
 * @brief Binds the given timestamp_tz value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The timestamp_tz value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_timestamp_tz(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_tz_t value);
/**
 * @brief Binds the given timestamp_ms value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The timestamp_ms value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_timestamp_ms(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_ms_t value);
/**
 * @brief Binds the given timestamp value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The timestamp value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_timestamp(
    koredb_prepared_statement* prepared_statement, const char* param_name,
    koredb_timestamp_t value);
/**
 * @brief Binds the given interval value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The interval value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_interval(
    koredb_prepared_statement* prepared_statement, const char* param_name, koredb_interval_t value);
/**
 * @brief Binds the given string value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The string value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_string(
    koredb_prepared_statement* prepared_statement, const char* param_name, const char* value);
/**
 * @brief Binds the given koredb value to the given parameter name in the prepared statement.
 * @param prepared_statement The prepared statement instance to bind the value.
 * @param param_name The parameter name to bind the value.
 * @param value The koredb value to bind.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_prepared_statement_bind_value(
    koredb_prepared_statement* prepared_statement, const char* param_name, koredb_value* value);

// QueryResult
/**
 * @brief Destroys the given query result instance.
 * @param query_result The query result instance to destroy.
 */
KOREDB_C_API void koredb_query_result_destroy(koredb_query_result* query_result);
/**
 * @brief Returns true if the query is executed successful, false otherwise.
 * @param query_result The query result instance to check.
 */
KOREDB_C_API bool koredb_query_result_is_success(koredb_query_result* query_result);
/**
 * @brief Returns true if the query result is partial because the query hit its timeout before
 * finishing. The tuples in the result are a valid prefix of the full result. Only ever true when
 * `enable_partial_result_on_timeout` was set for a read-only query.
 * @param query_result The query result instance to check.
 */
KOREDB_C_API bool koredb_query_result_is_truncated(koredb_query_result* query_result);
/**
 * @brief Returns why the result was truncated ("timeout" or "memory_limit"), or null if the result
 * is complete. The caller is responsible for freeing the returned string with
 * `koredb_destroy_string`.
 * @param query_result The query result instance to check.
 */
KOREDB_C_API char* koredb_query_result_get_truncation_reason(koredb_query_result* query_result);
/**
 * @brief Returns the error message if the query is failed.
 * The caller is responsible for freeing the returned string with `koredb_destroy_string`.
 * @param query_result The query result instance to check and return error message.
 * @return The error message if the query has failed, or null if the query is successful.
 */
KOREDB_C_API char* koredb_query_result_get_error_message(koredb_query_result* query_result);
/**
 * @brief Returns the number of columns in the query result.
 * @param query_result The query result instance to return.
 */
KOREDB_C_API uint64_t koredb_query_result_get_num_columns(koredb_query_result* query_result);
/**
 * @brief Returns the column name at the given index.
 * @param query_result The query result instance to return.
 * @param index The index of the column to return name.
 * @param[out] out_column_name The output parameter that will hold the column name.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_query_result_get_column_name(koredb_query_result* query_result,
    uint64_t index, char** out_column_name);
/**
 * @brief Returns the data type of the column at the given index.
 * @param query_result The query result instance to return.
 * @param index The index of the column to return data type.
 * @param[out] out_column_data_type The output parameter that will hold the column data type.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_query_result_get_column_data_type(
    koredb_query_result* query_result, uint64_t index, koredb_logical_type* out_column_data_type);
/**
 * @brief Returns the number of tuples in the query result.
 * @param query_result The query result instance to return.
 */
KOREDB_C_API uint64_t koredb_query_result_get_num_tuples(koredb_query_result* query_result);
/**
 * @brief Returns the query summary of the query result.
 * @param query_result The query result instance to return.
 * @param[out] out_query_summary The output parameter that will hold the query summary.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_query_result_get_query_summary(koredb_query_result* query_result,
    koredb_query_summary* out_query_summary);
/**
 * @brief Returns true if we have not consumed all tuples in the query result, false otherwise.
 * @param query_result The query result instance to check.
 */
KOREDB_C_API bool koredb_query_result_has_next(koredb_query_result* query_result);
/**
 * @brief Returns the next tuple in the query result. Throws an exception if there is no more tuple.
 * Note that to reduce resource allocation, all calls to koredb_query_result_get_next() reuse the
 * same FlatTuple object. Since its contents will be overwritten, please complete processing a
 * FlatTuple or make a copy of its data before calling koredb_query_result_get_next() again.
 * @param query_result The query result instance to return.
 * @param[out] out_flat_tuple The output parameter that will hold the next tuple.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_query_result_get_next(koredb_query_result* query_result,
    koredb_flat_tuple* out_flat_tuple);
/**
 * @brief Returns true if we have not consumed all query results, false otherwise. Use this function
 * for loop results of multiple query statements
 * @param query_result The query result instance to check.
 */
KOREDB_C_API bool koredb_query_result_has_next_query_result(koredb_query_result* query_result);
/**
 * @brief Returns the next query result. Use this function to loop multiple query statements'
 * results.
 * @param query_result The query result instance to return.
 * @param[out] out_next_query_result The output parameter that will hold the next query result.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_query_result_get_next_query_result(
    koredb_query_result* query_result, koredb_query_result* out_next_query_result);

/**
 * @brief Returns the query result as a string.
 * @param query_result The query result instance to return.
 * @return The query result as a string.
 */
KOREDB_C_API char* koredb_query_result_to_string(koredb_query_result* query_result);
/**
 * @brief Resets the iterator of the query result to the beginning of the query result.
 * @param query_result The query result instance to reset iterator.
 */
KOREDB_C_API void koredb_query_result_reset_iterator(koredb_query_result* query_result);

/**
 * @brief Returns the query result's schema as ArrowSchema.
 * @param query_result The query result instance to return.
 * @param[out] out_schema The output parameter that will hold the datatypes of the columns as an
 * arrow schema.
 * @return The state indicating the success or failure of the operation.
 *
 * It is the caller's responsibility to call the release function to release the underlying data
 */
KOREDB_C_API koredb_state koredb_query_result_get_arrow_schema(koredb_query_result* query_result,
    struct ArrowSchema* out_schema);

/**
 * @brief Returns the next chunk of the query result as ArrowArray.
 * @param query_result The query result instance to return.
 * @param chunk_size The number of tuples to return in the chunk.
 * @param[out] out_arrow_array The output parameter that will hold the arrow array representation of
 * the query result. The arrow array internally stores an arrow struct with fields for each of the
 * columns.
 * @return The state indicating the success or failure of the operation.
 *
 * It is the caller's responsibility to call the release function to release the underlying data
 */
KOREDB_C_API koredb_state koredb_query_result_get_next_arrow_chunk(
    koredb_query_result* query_result, int64_t chunk_size, struct ArrowArray* out_arrow_array);

// FlatTuple
/**
 * @brief Destroys the given flat tuple instance.
 * @param flat_tuple The flat tuple instance to destroy.
 */
KOREDB_C_API void koredb_flat_tuple_destroy(koredb_flat_tuple* flat_tuple);
/**
 * @brief Returns the value at index of the flat tuple.
 * @param flat_tuple The flat tuple instance to return.
 * @param index The index of the value to return.
 * @param[out] out_value The output parameter that will hold the value at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_flat_tuple_get_value(koredb_flat_tuple* flat_tuple, uint64_t index,
    koredb_value* out_value);
/**
 * @brief Converts the flat tuple to a string.
 * @param flat_tuple The flat tuple instance to convert.
 * @return The flat tuple as a string.
 */
KOREDB_C_API char* koredb_flat_tuple_to_string(koredb_flat_tuple* flat_tuple);

// DataType
// TODO(Chang): Refactor the datatype constructor to follow the cpp way of creating dataTypes.
/**
 * @brief Creates a data type instance with the given id, childType and num_elements_in_array.
 * Caller is responsible for destroying the returned data type instance.
 * @param id The enum type id of the datatype to create.
 * @param child_type The child type of the datatype to create(only used for nested dataTypes).
 * @param num_elements_in_array The number of elements in the array(only used for ARRAY).
 * @param[out] out_type The output parameter that will hold the data type instance.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API void koredb_data_type_create(koredb_data_type_id id, koredb_logical_type* child_type,
    uint64_t num_elements_in_array, koredb_logical_type* out_type);
/**
 * @brief Creates a new data type instance by cloning the given data type instance.
 * @param data_type The data type instance to clone.
 * @param[out] out_type The output parameter that will hold the cloned data type instance.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API void koredb_data_type_clone(koredb_logical_type* data_type,
    koredb_logical_type* out_type);
/**
 * @brief Destroys the given data type instance.
 * @param data_type The data type instance to destroy.
 */
KOREDB_C_API void koredb_data_type_destroy(koredb_logical_type* data_type);
/**
 * @brief Returns true if the given data type is equal to the other data type, false otherwise.
 * @param data_type1 The first data type instance to compare.
 * @param data_type2 The second data type instance to compare.
 */
KOREDB_C_API bool koredb_data_type_equals(koredb_logical_type* data_type1,
    koredb_logical_type* data_type2);
/**
 * @brief Returns the enum type id of the given data type.
 * @param data_type The data type instance to return.
 */
KOREDB_C_API koredb_data_type_id koredb_data_type_get_id(koredb_logical_type* data_type);
/**
 * @brief Returns the number of elements for array.
 * @param data_type The data type instance to return.
 * @param[out] out_result The output parameter that will hold the number of elements in the array.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_data_type_get_num_elements_in_array(koredb_logical_type* data_type,
    uint64_t* out_result);

// Value
/**
 * @brief Creates a NULL value of ANY type. Caller is responsible for destroying the returned value.
 */
KOREDB_C_API koredb_value* koredb_value_create_null();
/**
 * @brief Creates a value of the given data type. Caller is responsible for destroying the
 * returned value.
 * @param data_type The data type of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_null_with_data_type(koredb_logical_type* data_type);
/**
 * @brief Returns true if the given value is NULL, false otherwise.
 * @param value The value instance to check.
 */
KOREDB_C_API bool koredb_value_is_null(koredb_value* value);
/**
 * @brief Sets the given value to NULL or not.
 * @param value The value instance to set.
 * @param is_null True if sets the value to NULL, false otherwise.
 */
KOREDB_C_API void koredb_value_set_null(koredb_value* value, bool is_null);
/**
 * @brief Creates a value of the given data type with default non-NULL value. Caller is responsible
 * for destroying the returned value.
 * @param data_type The data type of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_default(koredb_logical_type* data_type);
/**
 * @brief Creates a value with boolean type and the given bool value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The bool value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_bool(bool val_);
/**
 * @brief Creates a value with int8 type and the given int8 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The int8 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_int8(int8_t val_);
/**
 * @brief Creates a value with int16 type and the given int16 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The int16 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_int16(int16_t val_);
/**
 * @brief Creates a value with int32 type and the given int32 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The int32 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_int32(int32_t val_);
/**
 * @brief Creates a value with int64 type and the given int64 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The int64 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_int64(int64_t val_);
/**
 * @brief Creates a value with uint8 type and the given uint8 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The uint8 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_uint8(uint8_t val_);
/**
 * @brief Creates a value with uint16 type and the given uint16 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The uint16 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_uint16(uint16_t val_);
/**
 * @brief Creates a value with uint32 type and the given uint32 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The uint32 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_uint32(uint32_t val_);
/**
 * @brief Creates a value with uint64 type and the given uint64 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The uint64 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_uint64(uint64_t val_);
/**
 * @brief Creates a value with int128 type and the given int128 value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The int128 value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_int128(koredb_int128_t val_);
/**
 * @brief Creates a value with float type and the given float value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The float value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_float(float val_);
/**
 * @brief Creates a value with double type and the given double value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The double value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_double(double val_);
/**
 * @brief Creates a value with internal_id type and the given internal_id value. Caller is
 * responsible for destroying the returned value.
 * @param val_ The internal_id value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_internal_id(koredb_internal_id_t val_);
/**
 * @brief Creates a value with date type and the given date value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The date value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_date(koredb_date_t val_);
/**
 * @brief Creates a value with timestamp_ns type and the given timestamp value. Caller is
 * responsible for destroying the returned value.
 * @param val_ The timestamp_ns value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_timestamp_ns(koredb_timestamp_ns_t val_);
/**
 * @brief Creates a value with timestamp_ms type and the given timestamp value. Caller is
 * responsible for destroying the returned value.
 * @param val_ The timestamp_ms value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_timestamp_ms(koredb_timestamp_ms_t val_);
/**
 * @brief Creates a value with timestamp_sec type and the given timestamp value. Caller is
 * responsible for destroying the returned value.
 * @param val_ The timestamp_sec value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_timestamp_sec(koredb_timestamp_sec_t val_);
/**
 * @brief Creates a value with timestamp_tz type and the given timestamp value. Caller is
 * responsible for destroying the returned value.
 * @param val_ The timestamp_tz value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_timestamp_tz(koredb_timestamp_tz_t val_);
/**
 * @brief Creates a value with timestamp type and the given timestamp value. Caller is responsible
 * for destroying the returned value.
 * @param val_ The timestamp value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_timestamp(koredb_timestamp_t val_);
/**
 * @brief Creates a value with interval type and the given interval value. Caller is responsible
 * for destroying the returned value.
 * @param val_ The interval value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_interval(koredb_interval_t val_);
/**
 * @brief Creates a value with string type and the given string value. Caller is responsible for
 * destroying the returned value.
 * @param val_ The string value of the value to create.
 */
KOREDB_C_API koredb_value* koredb_value_create_string(const char* val_);
/**
 * @brief Creates a list value with the given number of elements and the given elements.
 * The caller needs to make sure that all elements have the same type.
 * The elements are copied into the list value, so destroying the elements after creating the list
 * value is safe.
 * Caller is responsible for destroying the returned value.
 * @param num_elements The number of elements in the list.
 * @param elements The elements of the list.
 * @param[out] out_value The output parameter that will hold a pointer to the created list value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_create_list(uint64_t num_elements, koredb_value** elements,
    koredb_value** out_value);
/**
 * @brief Creates a struct value with the given number of fields and the given field names and
 * values. The caller needs to make sure that all field names are unique.
 * The field names and values are copied into the struct value, so destroying the field names and
 * values after creating the struct value is safe.
 * Caller is responsible for destroying the returned value.
 * @param num_fields The number of fields in the struct.
 * @param field_names The field names of the struct.
 * @param field_values The field values of the struct.
 * @param[out] out_value The output parameter that will hold a pointer to the created struct value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_create_struct(uint64_t num_fields, const char** field_names,
    koredb_value** field_values, koredb_value** out_value);
/**
 * @brief Creates a map value with the given number of fields and the given keys and values. The
 * caller needs to make sure that all keys are unique, and all keys and values have the same type.
 * The keys and values are copied into the map value, so destroying the keys and values after
 * creating the map value is safe.
 * Caller is responsible for destroying the returned value.
 * @param num_fields The number of fields in the map.
 * @param keys The keys of the map.
 * @param values The values of the map.
 * @param[out] out_value The output parameter that will hold a pointer to the created map value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_create_map(uint64_t num_fields, koredb_value** keys,
    koredb_value** values, koredb_value** out_value);
/**
 * @brief Creates a new value based on the given value. Caller is responsible for destroying the
 * returned value.
 * @param value The value to create from.
 */
KOREDB_C_API koredb_value* koredb_value_clone(koredb_value* value);
/**
 * @brief Copies the other value to the value.
 * @param value The value to copy to.
 * @param other The value to copy from.
 */
KOREDB_C_API void koredb_value_copy(koredb_value* value, koredb_value* other);
/**
 * @brief Destroys the value.
 * @param value The value to destroy.
 */
KOREDB_C_API void koredb_value_destroy(koredb_value* value);
/**
 * @brief Returns the number of elements per list of the given value. The value must be of type
 * ARRAY.
 * @param value The ARRAY value to get list size.
 * @param[out] out_result The output parameter that will hold the number of elements per list.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_list_size(koredb_value* value, uint64_t* out_result);
/**
 * @brief Returns the element at index of the given value. The value must be of type LIST.
 * @param value The LIST value to return.
 * @param index The index of the element to return.
 * @param[out] out_value The output parameter that will hold the element at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_list_element(koredb_value* value, uint64_t index,
    koredb_value* out_value);
/**
 * @brief Returns the number of fields of the given struct value. The value must be of type STRUCT.
 * @param value The STRUCT value to get number of fields.
 * @param[out] out_result The output parameter that will hold the number of fields.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_struct_num_fields(koredb_value* value,
    uint64_t* out_result);
/**
 * @brief Returns the field name at index of the given struct value. The value must be of physical
 * type STRUCT (STRUCT, NODE, REL, RECURSIVE_REL, UNION).
 * @param value The STRUCT value to get field name.
 * @param index The index of the field name to return.
 * @param[out] out_result The output parameter that will hold the field name at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_struct_field_name(koredb_value* value, uint64_t index,
    char** out_result);
/**
 * @brief Returns the field value at index of the given struct value. The value must be of physical
 * type STRUCT (STRUCT, NODE, REL, RECURSIVE_REL, UNION).
 * @param value The STRUCT value to get field value.
 * @param index The index of the field value to return.
 * @param[out] out_value The output parameter that will hold the field value at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_struct_field_value(koredb_value* value, uint64_t index,
    koredb_value* out_value);

/**
 * @brief Returns the size of the given map value. The value must be of type MAP.
 * @param value The MAP value to get size.
 * @param[out] out_result The output parameter that will hold the size of the map.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_map_size(koredb_value* value, uint64_t* out_result);
/**
 * @brief Returns the key at index of the given map value. The value must be of physical
 * type MAP.
 * @param value The MAP value to get key.
 * @param index The index of the field name to return.
 * @param[out] out_key The output parameter that will hold the key at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_map_key(koredb_value* value, uint64_t index,
    koredb_value* out_key);
/**
 * @brief Returns the field value at index of the given map value. The value must be of physical
 * type MAP.
 * @param value The MAP value to get field value.
 * @param index The index of the field value to return.
 * @param[out] out_value The output parameter that will hold the field value at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_map_value(koredb_value* value, uint64_t index,
    koredb_value* out_value);
/**
 * @brief Returns the list of nodes for recursive rel value. The value must be of type
 * RECURSIVE_REL.
 * @param value The RECURSIVE_REL value to return.
 * @param[out] out_value The output parameter that will hold the list of nodes.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_recursive_rel_node_list(koredb_value* value,
    koredb_value* out_value);

/**
 * @brief Returns the list of rels for recursive rel value. The value must be of type RECURSIVE_REL.
 * @param value The RECURSIVE_REL value to return.
 * @param[out] out_value The output parameter that will hold the list of rels.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_recursive_rel_rel_list(koredb_value* value,
    koredb_value* out_value);
/**
 * @brief Returns internal type of the given value.
 * @param value The value to return.
 * @param[out] out_type The output parameter that will hold the internal type of the value.
 */
KOREDB_C_API void koredb_value_get_data_type(koredb_value* value, koredb_logical_type* out_type);
/**
 * @brief Returns the boolean value of the given value. The value must be of type BOOL.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the boolean value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_bool(koredb_value* value, bool* out_result);
/**
 * @brief Returns the int8 value of the given value. The value must be of type INT8.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the int8 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_int8(koredb_value* value, int8_t* out_result);
/**
 * @brief Returns the int16 value of the given value. The value must be of type INT16.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the int16 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_int16(koredb_value* value, int16_t* out_result);
/**
 * @brief Returns the int32 value of the given value. The value must be of type INT32.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the int32 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_int32(koredb_value* value, int32_t* out_result);
/**
 * @brief Returns the int64 value of the given value. The value must be of type INT64 or SERIAL.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the int64 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_int64(koredb_value* value, int64_t* out_result);
/**
 * @brief Returns the uint8 value of the given value. The value must be of type UINT8.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the uint8 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_uint8(koredb_value* value, uint8_t* out_result);
/**
 * @brief Returns the uint16 value of the given value. The value must be of type UINT16.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the uint16 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_uint16(koredb_value* value, uint16_t* out_result);
/**
 * @brief Returns the uint32 value of the given value. The value must be of type UINT32.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the uint32 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_uint32(koredb_value* value, uint32_t* out_result);
/**
 * @brief Returns the uint64 value of the given value. The value must be of type UINT64.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the uint64 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_uint64(koredb_value* value, uint64_t* out_result);
/**
 * @brief Returns the int128 value of the given value. The value must be of type INT128.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the int128 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_int128(koredb_value* value, koredb_int128_t* out_result);
/**
 * @brief convert a string to int128 value.
 * @param str The string to convert.
 * @param[out] out_result The output parameter that will hold the int128 value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_int128_t_from_string(const char* str, koredb_int128_t* out_result);
/**
 * @brief convert int128 to corresponding string.
 * @param val The int128 value to convert.
 * @param[out] out_result The output parameter that will hold the string value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_int128_t_to_string(koredb_int128_t val, char** out_result);
/**
 * @brief Returns the float value of the given value. The value must be of type FLOAT.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the float value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_float(koredb_value* value, float* out_result);
/**
 * @brief Returns the double value of the given value. The value must be of type DOUBLE.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the double value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_double(koredb_value* value, double* out_result);
/**
 * @brief Returns the internal id value of the given value. The value must be of type INTERNAL_ID.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the internal id value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_internal_id(koredb_value* value,
    koredb_internal_id_t* out_result);
/**
 * @brief Returns the date value of the given value. The value must be of type DATE.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the date value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_date(koredb_value* value, koredb_date_t* out_result);
/**
 * @brief Returns the timestamp value of the given value. The value must be of type TIMESTAMP.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the timestamp value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_timestamp(koredb_value* value,
    koredb_timestamp_t* out_result);
/**
 * @brief Returns the timestamp_ns value of the given value. The value must be of type TIMESTAMP_NS.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the timestamp_ns value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_timestamp_ns(koredb_value* value,
    koredb_timestamp_ns_t* out_result);
/**
 * @brief Returns the timestamp_ms value of the given value. The value must be of type TIMESTAMP_MS.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the timestamp_ms value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_timestamp_ms(koredb_value* value,
    koredb_timestamp_ms_t* out_result);
/**
 * @brief Returns the timestamp_sec value of the given value. The value must be of type
 * TIMESTAMP_SEC.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the timestamp_sec value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_timestamp_sec(koredb_value* value,
    koredb_timestamp_sec_t* out_result);
/**
 * @brief Returns the timestamp_tz value of the given value. The value must be of type TIMESTAMP_TZ.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the timestamp_tz value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_timestamp_tz(koredb_value* value,
    koredb_timestamp_tz_t* out_result);
/**
 * @brief Returns the interval value of the given value. The value must be of type INTERVAL.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the interval value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_interval(koredb_value* value,
    koredb_interval_t* out_result);
/**
 * @brief Returns the decimal value of the given value as a string. The value must be of type
 * DECIMAL.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the decimal value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_decimal_as_string(koredb_value* value,
    char** out_result);
/**
 * @brief Returns the string value of the given value. The value must be of type STRING.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the string value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_string(koredb_value* value, char** out_result);
/**
 * @brief Returns the blob value of the given value. The returned buffer is null-terminated similar
 * to a string. The value must be of type BLOB.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the blob value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_blob(koredb_value* value, uint8_t** out_result);
/**
 * @brief Returns the uuid value of the given value.
 * to a string. The value must be of type UUID.
 * @param value The value to return.
 * @param[out] out_result The output parameter that will hold the uuid value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_value_get_uuid(koredb_value* value, char** out_result);
/**
 * @brief Converts the given value to string.
 * @param value The value to convert.
 * @return The value as a string.
 */
KOREDB_C_API char* koredb_value_to_string(koredb_value* value);
/**
 * @brief Returns the internal id value of the given node value as a koredb value.
 * @param node_val The node value to return.
 * @param[out] out_value The output parameter that will hold the internal id value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_get_id_val(koredb_value* node_val,
    koredb_value* out_value);
/**
 * @brief Returns the label value of the given node value as a label value.
 * @param node_val The node value to return.
 * @param[out] out_value The output parameter that will hold the label value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_get_label_val(koredb_value* node_val,
    koredb_value* out_value);
/**
 * @brief Returns the number of properties of the given node value.
 * @param node_val The node value to return.
 * @param[out] out_value The output parameter that will hold the number of properties.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_get_property_size(koredb_value* node_val,
    uint64_t* out_value);
/**
 * @brief Returns the property name of the given node value at the given index.
 * @param node_val The node value to return.
 * @param index The index of the property.
 * @param[out] out_result The output parameter that will hold the property name at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_get_property_name_at(koredb_value* node_val,
    uint64_t index, char** out_result);
/**
 * @brief Returns the property value of the given node value at the given index.
 * @param node_val The node value to return.
 * @param index The index of the property.
 * @param[out] out_value The output parameter that will hold the property value at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_get_property_value_at(koredb_value* node_val,
    uint64_t index, koredb_value* out_value);
/**
 * @brief Converts the given node value to string.
 * @param node_val The node value to convert.
 * @param[out] out_result The output parameter that will hold the node value as a string.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_node_val_to_string(koredb_value* node_val, char** out_result);
/**
 * @brief Returns the internal id value of the rel value as a koredb value.
 * @param rel_val The rel value to return.
 * @param[out] out_value The output parameter that will hold the internal id value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_id_val(koredb_value* rel_val, koredb_value* out_value);
/**
 * @brief Returns the internal id value of the source node of the given rel value as a koredb value.
 * @param rel_val The rel value to return.
 * @param[out] out_value The output parameter that will hold the internal id value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_src_id_val(koredb_value* rel_val,
    koredb_value* out_value);
/**
 * @brief Returns the internal id value of the destination node of the given rel value as a koredb
 * value.
 * @param rel_val The rel value to return.
 * @param[out] out_value The output parameter that will hold the internal id value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_dst_id_val(koredb_value* rel_val,
    koredb_value* out_value);
/**
 * @brief Returns the label value of the given rel value.
 * @param rel_val The rel value to return.
 * @param[out] out_value The output parameter that will hold the label value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_label_val(koredb_value* rel_val,
    koredb_value* out_value);
/**
 * @brief Returns the number of properties of the given rel value.
 * @param rel_val The rel value to return.
 * @param[out] out_value The output parameter that will hold the number of properties.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_property_size(koredb_value* rel_val,
    uint64_t* out_value);
/**
 * @brief Returns the property name of the given rel value at the given index.
 * @param rel_val The rel value to return.
 * @param index The index of the property.
 * @param[out] out_result The output parameter that will hold the property name at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_property_name_at(koredb_value* rel_val, uint64_t index,
    char** out_result);
/**
 * @brief Returns the property of the given rel value at the given index as koredb value.
 * @param rel_val The rel value to return.
 * @param index The index of the property.
 * @param[out] out_value The output parameter that will hold the property value at index.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_get_property_value_at(koredb_value* rel_val,
    uint64_t index, koredb_value* out_value);
/**
 * @brief Converts the given rel value to string.
 * @param rel_val The rel value to convert.
 * @param[out] out_result The output parameter that will hold the rel value as a string.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_rel_val_to_string(koredb_value* rel_val, char** out_result);
/**
 * @brief Destroys any string created by the KoreDB C API, including both the error message and the
 * values returned by the API functions. This function is provided to avoid the inconsistency
 * between the memory allocation and deallocation across different libraries and is preferred over
 * using the standard C free function.
 * @param str The string to destroy.
 */
KOREDB_C_API void koredb_destroy_string(char* str);
/**
 * @brief Destroys any blob created by the KoreDB C API. This function is provided to avoid the
 * inconsistency between the memory allocation and deallocation across different libraries and
 * is preferred over using the standard C free function.
 * @param blob The blob to destroy.
 */
KOREDB_C_API void koredb_destroy_blob(uint8_t* blob);

// QuerySummary
/**
 * @brief Destroys the given query summary.
 * @param query_summary The query summary to destroy.
 */
KOREDB_C_API void koredb_query_summary_destroy(koredb_query_summary* query_summary);
/**
 * @brief Returns the compilation time of the given query summary in milliseconds.
 * @param query_summary The query summary to get compilation time.
 */
KOREDB_C_API double koredb_query_summary_get_compiling_time(koredb_query_summary* query_summary);
/**
 * @brief Returns the execution time of the given query summary in milliseconds.
 * @param query_summary The query summary to get execution time.
 */
KOREDB_C_API double koredb_query_summary_get_execution_time(koredb_query_summary* query_summary);

// Utility functions
/**
 * @brief Convert timestamp_ns to corresponding tm struct.
 * @param timestamp The timestamp_ns value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_ns_to_tm(koredb_timestamp_ns_t timestamp,
    struct tm* out_result);
/**
 * @brief Convert timestamp_ms to corresponding tm struct.
 * @param timestamp The timestamp_ms value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_ms_to_tm(koredb_timestamp_ms_t timestamp,
    struct tm* out_result);
/**
 * @brief Convert timestamp_sec to corresponding tm struct.
 * @param timestamp The timestamp_sec value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_sec_to_tm(koredb_timestamp_sec_t timestamp,
    struct tm* out_result);
/**
 * @brief Convert timestamp_tz to corresponding tm struct.
 * @param timestamp The timestamp_tz value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_tz_to_tm(koredb_timestamp_tz_t timestamp,
    struct tm* out_result);
/**
 * @brief Convert timestamp to corresponding tm struct.
 * @param timestamp The timestamp value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_to_tm(koredb_timestamp_t timestamp,
    struct tm* out_result);
/**
 * @brief Convert tm struct to timestamp_ns value.
 * @param tm The tm struct to convert.
 * @param[out] out_result The output parameter that will hold the timestamp_ns value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_ns_from_tm(struct tm tm,
    koredb_timestamp_ns_t* out_result);
/**
 * @brief Convert tm struct to timestamp_ms value.
 * @param tm The tm struct to convert.
 * @param[out] out_result The output parameter that will hold the timestamp_ms value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_ms_from_tm(struct tm tm,
    koredb_timestamp_ms_t* out_result);
/**
 * @brief Convert tm struct to timestamp_sec value.
 * @param tm The tm struct to convert.
 * @param[out] out_result The output parameter that will hold the timestamp_sec value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_sec_from_tm(struct tm tm,
    koredb_timestamp_sec_t* out_result);
/**
 * @brief Convert tm struct to timestamp_tz value.
 * @param tm The tm struct to convert.
 * @param[out] out_result The output parameter that will hold the timestamp_tz value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_tz_from_tm(struct tm tm,
    koredb_timestamp_tz_t* out_result);
/**
 * @brief Convert timestamp_ns to corresponding string.
 * @param timestamp The timestamp_ns value to convert.
 * @param[out] out_result The output parameter that will hold the string value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_timestamp_from_tm(struct tm tm, koredb_timestamp_t* out_result);
/**
 * @brief Convert date to corresponding string.
 * @param date The date value to convert.
 * @param[out] out_result The output parameter that will hold the string value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_date_to_string(koredb_date_t date, char** out_result);
/**
 * @brief Convert a string to date value.
 * @param str The string to convert.
 * @param[out] out_result The output parameter that will hold the date value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_date_from_string(const char* str, koredb_date_t* out_result);
/**
 * @brief Convert date to corresponding tm struct.
 * @param date The date value to convert.
 * @param[out] out_result The output parameter that will hold the tm struct.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_date_to_tm(koredb_date_t date, struct tm* out_result);
/**
 * @brief Convert tm struct to date value.
 * @param tm The tm struct to convert.
 * @param[out] out_result The output parameter that will hold the date value.
 * @return The state indicating the success or failure of the operation.
 */
KOREDB_C_API koredb_state koredb_date_from_tm(struct tm tm, koredb_date_t* out_result);
/**
 * @brief Convert interval to corresponding difftime value in seconds.
 * @param interval The interval value to convert.
 * @param[out] out_result The output parameter that will hold the difftime value.
 */
KOREDB_C_API void koredb_interval_to_difftime(koredb_interval_t interval, double* out_result);
/**
 * @brief Convert difftime value in seconds to interval.
 * @param difftime The difftime value to convert.
 * @param[out] out_result The output parameter that will hold the interval value.
 */
KOREDB_C_API void koredb_interval_from_difftime(double difftime, koredb_interval_t* out_result);

// Version
/**
 * @brief Returns the version of the KoreDB library.
 */
KOREDB_C_API char* koredb_get_version();

/**
 * @brief Returns the storage version of the KoreDB library.
 */
KOREDB_C_API uint64_t koredb_get_storage_version();
#undef KOREDB_C_API
