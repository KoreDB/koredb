add_library(koredb_hnsw_index
        OBJECT
        hnsw_config.cpp
        hnsw_index.cpp
        hnsw_index_utils.cpp
        hnsw_rel_batch_insert.cpp
        hnsw_graph.cpp)

set(VECTOR_EXTENSION_OBJECT_FILES
        ${VECTOR_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_hnsw_index>
        PARENT_SCOPE)
