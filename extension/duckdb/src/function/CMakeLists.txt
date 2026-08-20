add_library(koredb_duckdb_function
        OBJECT
        clear_cache.cpp
        duckdb_scan.cpp)

set(DUCKDB_EXTENSION_OBJECT_FILES
        ${DUCKDB_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_duckdb_function>
        PARENT_SCOPE)
