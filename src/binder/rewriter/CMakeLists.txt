add_library(
        koredb_binder_rewriter
        OBJECT
        match_clause_pattern_label_rewriter.cpp
        normalized_query_part_match_rewriter.cpp
        with_clause_projection_rewriter.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_binder_rewriter>
        PARENT_SCOPE)
