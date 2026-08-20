add_library(koredb_rs STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/koredb_rs.cpp
    ${KOREDB_RS_BINDINGS_DIR}/sources/koredb/src/lib.rs.cc
)

target_include_directories(koredb_rs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${KOREDB_RS_BINDINGS_DIR}/include
        ${KOREDB_RS_BINDINGS_DIR}/crate
        ${KOREDB_RS_BINDINGS_DIR}/sources
)

target_link_libraries(koredb_rs koredb)
