add_library(koredb_fts_index
        OBJECT
        fts_index.cpp
        fts_internal_table_info.cpp
        fts_update_state.cpp)

set(FTS_EXTENSION_OBJECT_FILES
        ${FTS_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_fts_index>
        PARENT_SCOPE)
