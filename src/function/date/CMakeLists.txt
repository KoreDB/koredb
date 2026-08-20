add_library(koredb_function_date
        OBJECT
        date_functions.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_function_date>
        PARENT_SCOPE)
