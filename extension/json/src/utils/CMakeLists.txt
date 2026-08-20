add_library(koredb_json_utils
        OBJECT
        json_utils.cpp)

set(JSON_EXTENSION_OBJECT_FILES
        ${JSON_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_json_utils>
        PARENT_SCOPE)
