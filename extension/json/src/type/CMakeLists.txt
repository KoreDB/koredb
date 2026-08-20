add_library(koredb_json_type
        OBJECT
        json_type.cpp)

set(JSON_EXTENSION_OBJECT_FILES
        ${JSON_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_json_type>
        PARENT_SCOPE)
