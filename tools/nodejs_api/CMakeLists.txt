add_definitions(-DNAPI_VERSION=6)
add_definitions(-DNODE_RUNTIME=node)
add_definitions(-DBUILDING_NODE_EXTENSION)

# If on Windows use npx.cmd instead of npx
if(WIN32)
  set(NPX_CMD npx.cmd)
else()
  set(NPX_CMD npx)
endif()
set(NODE_CMD node)

# cmake-js prints diagnostic "info ..." / "ERR! ..." lines to stdout alongside
# the value it is asked to print, and on Windows those lines (and the value
# itself) contain backslash paths that break CMake string parsing (e.g. "Invalid
# character escape '\P'"). Drop the diagnostics and normalize path separators.
#
# Every remaining line is kept, as a CMake list. print-cmakejs-include
# legitimately returns MORE THAN ONE directory (the Node-API headers plus
# node-addon-api); keeping only the last line silently discarded the Node
# headers, which surfaced much later in the build as
# "fatal error: node_api.h: No such file or directory".
macro(clean_cmakejs_var _var)
  string(REPLACE "\r\n" "\n" _ccv_raw "${${_var}}")
  string(REPLACE "\n" ";" _ccv_lines "${_ccv_raw}")
  set(_ccv_keep "")
  foreach(_ccv_line IN LISTS _ccv_lines)
    string(STRIP "${_ccv_line}" _ccv_line)
    if(NOT "${_ccv_line}" STREQUAL "")
      if(NOT "${_ccv_line}" MATCHES "^(info|ERR!|WARN|warn|verb|sill|http|npm) ")
        string(REPLACE "\\" "/" _ccv_line "${_ccv_line}")
        list(APPEND _ccv_keep "${_ccv_line}")
      endif()
    endif()
  endforeach()
  set(${_var} "${_ccv_keep}")
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

# On Windows a Node-API addon still has to link against an import library that
# resolves the napi_* symbols out of the host executable. `cmake-js
# print-cmakejs-lib` reports where that library belongs - <nodejs_api>/build/
# node.lib - but it never creates it: cmake-js only generates node.lib when it
# drives the build itself, where it passes CMAKE_JS_NODELIB_DEF/TARGET and
# leaves the actual `lib /def:` call to the project. This build invokes CMake
# directly, so the generation has to happen here, from the .def that ships in
# node-api-headers. Without it the link step fails with:
#   ninja: error: '.../nodejs_api/build/node.lib', needed by 'koredbjs.node',
#   missing and no known rule to make it
if(MSVC)
  execute_process(
    COMMAND ${NODE_CMD} -p "require('node-api-headers').def_paths.node_api_def"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_API_DEF
    RESULT_VARIABLE NODE_API_DEF_RESULT
    ERROR_VARIABLE NODE_API_DEF_ERROR
  )
  if(NOT NODE_API_DEF_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Could not locate node-api-headers. Run `npm install` in "
      "${CMAKE_CURRENT_SOURCE_DIR} first. ${NODE_API_DEF_ERROR}")
  endif()
  clean_cmakejs_var(NODE_API_DEF)

  # CMAKE_STATIC_LINKER_FLAGS carries the /machine: flag CMake derived for this
  # target, which is what stops the arm64 build from emitting an x86 stub.
  separate_arguments(NODE_LIB_AR_FLAGS NATIVE_COMMAND "${CMAKE_STATIC_LINKER_FLAGS}")

  foreach(NODE_LIB_PATH IN LISTS CMAKE_JS_LIB)
    get_filename_component(NODE_LIB_DIR "${NODE_LIB_PATH}" DIRECTORY)
    file(MAKE_DIRECTORY "${NODE_LIB_DIR}")
    execute_process(
      COMMAND ${CMAKE_AR} /def:${NODE_API_DEF} /out:${NODE_LIB_PATH} ${NODE_LIB_AR_FLAGS}
      RESULT_VARIABLE NODE_LIB_RESULT
      OUTPUT_VARIABLE NODE_LIB_OUTPUT
      ERROR_VARIABLE NODE_LIB_OUTPUT
    )
    if(NOT NODE_LIB_RESULT EQUAL 0)
      message(FATAL_ERROR
        "Failed to generate ${NODE_LIB_PATH} from ${NODE_API_DEF}: "
        "${NODE_LIB_OUTPUT}")
    endif()
    message(STATUS "Generated Node-API import library: ${NODE_LIB_PATH}")
  endforeach()
endif()

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

# Electron compatibility.
#
# The addon resolves the napi_* symbols out of whatever executable is hosting
# it. The import library generated above comes from node-api-headers' .def,
# which starts with `NAME NODE.EXE`, so node.exe lands in the *normal* import
# table and the Windows loader insists on a module literally called node.exe.
# Inside electron.exe there is none: on a clean machine the load fails with
# "The specified module could not be found", and where a node.exe happens to
# sit on PATH the loader maps a second Node image into the process, which
# takes the host down with it.
#
# Delay-loading node.exe hands the resolution to cmake-js's
# win_delay_load_hook.cc - already compiled in through ${CMAKE_JS_SRC} above,
# but inert without this flag. The hook answers with GetModuleHandle(NULL),
# i.e. the running host whatever it is called, so one binary loads in node.exe,
# electron.exe and any renamed application executable alike.
#
# scripts/verify-addon.ts asserts the result on the produced binary.
if(MSVC)
  target_link_options(koredbjs PRIVATE /DELAYLOAD:NODE.EXE)
  target_link_libraries(koredbjs PRIVATE delayimp)
endif()
