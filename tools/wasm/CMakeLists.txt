add_executable(koredb_wasm
    src_cpp/main.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/connection.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/database.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/data_type.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/helpers.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/flat_tuple.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/prepared_statement.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/query_result.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/query_summary.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/value.cpp
    ${PROJECT_SOURCE_DIR}/src/c_api/version.cpp
)

file(GLOB JS_SOURCE_FILES ./src_js/*)

target_link_libraries(koredb_wasm PRIVATE koredb)

set_target_properties(koredb_wasm
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/koredb"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/koredb"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/koredb"
)

file(COPY ${JS_SOURCE_FILES} DESTINATION  "${CMAKE_CURRENT_SOURCE_DIR}/build")
