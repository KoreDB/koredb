add_library(koredb_function_internal_id
        OBJECT
        internal_id_creation_function.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_function_internal_id>
        PARENT_SCOPE)
