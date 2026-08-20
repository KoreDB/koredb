add_library(koredb_parser_antlr_parser
        OBJECT
        koredb_cypher_parser.cpp
        parser_error_listener.cpp
        parser_error_strategy.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_parser_antlr_parser>
        PARENT_SCOPE)
