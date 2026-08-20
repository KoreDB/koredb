add_library(koredb_buffered_json_reader
        OBJECT
        buffered_json_reader.cpp)

set(JSON_EXTENSION_OBJECT_FILES
        ${JSON_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_buffered_json_reader>
        PARENT_SCOPE)
