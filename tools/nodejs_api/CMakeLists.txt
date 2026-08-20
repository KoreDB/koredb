add_definitions(-DNAPI_VERSION=6)
add_definitions(-DNODE_RUNTIME=node)
add_definitions(-DBUILDING_NODE_EXTENSION)

# If on Windows use npx.cmd instead of npx
if(WIN32)
  set(NPX_CMD npx.cmd)
else()
  set(NPX_CMD npx)
endif()

# cmake-js prints diagnostic "info ..." lines to stdout before the actual value
# it is asked to print, and on Windows those lines (and the value itself) contain
# backslash paths that break CMake string parsing (e.g. "Invalid character escape
# '\P'"). Keep only the last non-empty line of the captured output and normalize
# path separators. On platforms where cmake-js emits a single clean line this is
# a no-op.
macro(clean_cmakejs_var _var)
  string(REPLACE "\r\n" "\n" _ccv_tmp "${${_var}}")
  string(STRIP "${_ccv_tmp}" _ccv_tmp)
  string(REPLACE "\n" ";" _ccv_lines "${_ccv_tmp}")
  list(LENGTH _ccv_lines _ccv_len)
  if(_ccv_len GREATER 0)
    math(EXPR _ccv_last "${_ccv_len} - 1")
    list(GET _ccv_lines ${_ccv_last} _ccv_tmp)
  endif()
  string(STRIP "${_ccv_tmp}" _ccv_tmp)
  string(REPLACE "\\" "/" _ccv_tmp "${_ccv_tmp}")
  set(${_var} "${_ccv_tmp}")
endmacro()

execute_process(
  COMMAND ${NPX_CMD} cmake-js print-cmakejs-include
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CMAKE_JS_INC
)
execute_process(
  COMMAND ${NPX_CMD} cmake-js print-cmakejs-lib
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CMAKE_JS_LIB
)
execute_process(
  COMMAND ${NPX_CMD} cmake-js print-cmakejs-src
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CMAKE_JS_SRC
)

clean_cmakejs_var(CMAKE_JS_INC)
clean_cmakejs_var(CMAKE_JS_LIB)
clean_cmakejs_var(CMAKE_JS_SRC)

# Print CMAKE_JS variables
message(STATUS "CMake.js configurations: LIB=${CMAKE_JS_LIB}, INC=${CMAKE_JS_INC}, SRC=${CMAKE_JS_SRC}")

get_filename_component(NODE_ADDON_API_INCLUDE_PATH ./node_modules/node-addon-api ABSOLUTE)

include_directories(${CMAKE_JS_INC})
include_directories(${NODE_ADDON_API_INCLUDE_PATH})

file(GLOB CPP_SOURCE_FILES ./src_cpp/*)
file(GLOB JS_SOURCE_FILES 
    ./src_js/*.js
    ./src_js/*.mjs
    ./src_js/*.d.ts
)
# Copy all JS/TS files to build directory
file(COPY ${JS_SOURCE_FILES} DESTINATION  "${CMAKE_CURRENT_SOURCE_DIR}/build")

# Give the build directory a manifest so that it is a usable npm package on its
# own. A source build is consumed with `npm link tools/nodejs_api/build` (or by
# installing that path), which is the documented route for platforms that have
# no prebuilt addon package. No optionalDependencies here: the addon is built
# right next to this file, and the loader prefers a local koredbjs.node.
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/build/package.json"
"{
  \"name\": \"@koredb/koredb\",
  \"version\": \"${CMAKE_PROJECT_VERSION}\",
  \"description\": \"Locally built KoreDB Node.js API.\",
  \"license\": \"MIT\",
  \"main\": \"index.js\",
  \"module\": \"./index.mjs\",
  \"types\": \"./koredb.d.ts\",
  \"exports\": {
    \".\": {
      \"require\": \"./index.js\",
      \"import\": \"./index.mjs\",
      \"types\": \"./koredb.d.ts\"
    },
    \"./package.json\": \"./package.json\"
  },
  \"type\": \"commonjs\"
}
")

add_library(koredbjs SHARED ${CPP_SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(koredbjs PROPERTIES PREFIX "" SUFFIX ".node")
set_target_properties(koredbjs
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
if(APPLE)
  target_link_options(koredbjs PRIVATE -undefined dynamic_lookup)
endif()
target_link_libraries(koredbjs PRIVATE koredb ${CMAKE_JS_LIB})
