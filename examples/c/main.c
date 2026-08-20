#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "koredb.h"

int main() {
    koredb_database db;
    koredb_connection conn;
    koredb_database_init("" /* fill db path */, koredb_default_system_config(), &db);
    koredb_connection_init(&db, &conn);

    // Create schema.
    koredb_query_result result;
    koredb_connection_query(
        &conn, "CREATE NODE TABLE Person(name STRING, age INT64, PRIMARY KEY(name));", &result);
    koredb_query_result_destroy(&result);
    // Create nodes.
    koredb_connection_query(&conn, "CREATE (:Person {name: 'Alice', age: 25});", &result);
    koredb_query_result_destroy(&result);
    koredb_connection_query(&conn, "CREATE (:Person {name: 'Bob', age: 30});", &result);
    koredb_query_result_destroy(&result);

    // Execute a simple query.
    koredb_connection_query(&conn, "MATCH (a:Person) RETURN a.name AS NAME, a.age AS AGE;", &result);

    // Fetch each value.
    koredb_flat_tuple tuple;
    koredb_value value;
    while (koredb_query_result_has_next(&result)) {
        koredb_query_result_get_next(&result, &tuple);

        koredb_flat_tuple_get_value(&tuple, 0, &value);
        char* name;
        koredb_value_get_string(&value, &name);

        koredb_flat_tuple_get_value(&tuple, 1, &value);
        int64_t age;
        koredb_value_get_int64(&value, &age);

        printf("name: %s, age: %" PRIi64 " \n", name, age);
        koredb_destroy_string(name);
    }
    koredb_value_destroy(&value);
    koredb_flat_tuple_destroy(&tuple);

    // Print query result.
    char* result_string = koredb_query_result_to_string(&result);
    printf("%s", result_string);
    koredb_destroy_string(result_string);

    koredb_query_result_destroy(&result);
    koredb_connection_destroy(&conn);
    koredb_database_destroy(&db);
    return 0;
}
