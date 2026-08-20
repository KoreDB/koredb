add_library(koredb_function_cast
        OBJECT
        cast_array.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_function_cast>
        PARENT_SCOPE)
