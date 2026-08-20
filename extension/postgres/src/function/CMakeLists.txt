add_library(koredb_postgres_function
        OBJECT
        sql_query.cpp)

set(POSTGRES_EXTENSION_OBJECT_FILES
        ${POSTGRES_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_postgres_function>
        PARENT_SCOPE)
