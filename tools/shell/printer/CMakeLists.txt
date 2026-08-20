include_directories(
    ${PROJECT_SOURCE_DIR}/third_party/yyjson/src
    ${PROJECT_SOURCE_DIR}/extension/json/src/include
)

add_library(koredb_shell_printer
        OBJECT
        json_printer.cpp
        printer_factory.cpp
        ${PROJECT_SOURCE_DIR}/extension/json/src/utils/json_utils.cpp
        ${PROJECT_SOURCE_DIR}/extension/json/src/type/json_type.cpp
)

set(KOREDB_SHELL_OBJECT_FILES
        ${KOREDB_SHELL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_shell_printer>
        PARENT_SCOPE)
