add_library(koredb_function_sequence
        OBJECT
        sequence_functions.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_function_sequence>
        PARENT_SCOPE)
