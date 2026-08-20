add_library(koredb_duckdb_catalog
        OBJECT
        duckdb_catalog.cpp
        duckdb_table_catalog_entry.cpp)

set(DUCKDB_EXTENSION_OBJECT_FILES
        ${DUCKDB_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_duckdb_catalog>
        PARENT_SCOPE)
