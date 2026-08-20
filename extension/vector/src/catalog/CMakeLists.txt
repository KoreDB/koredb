add_library(koredb_hnsw_index_catalog
        OBJECT
        hnsw_index_catalog_entry.cpp)

set(VECTOR_EXTENSION_OBJECT_FILES
        ${VECTOR_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_hnsw_index_catalog>
        PARENT_SCOPE)
