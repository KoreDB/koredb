set(EXTENSION_LIST azure delta duckdb fts httpfs iceberg json llm postgres sqlite unity_catalog vector neo4j algo)


if (BUILD_EXTENSION_RELEASE)
    # This builds standalone extension artifacts, not the Release database or Node.js addon.
    message(STATUS "Building standalone extension release artifacts; extension static linking is disabled.")
else()
    # Embed these extensions in normal builds, including Release and Node.js builds.
    set(EXTENSION_STATIC_LINK_LIST algo fts json vector)
endif()
string(JOIN ", " joined_extensions ${EXTENSION_STATIC_LINK_LIST})
message(STATUS "Static link extensions: ${joined_extensions}")
foreach(extension IN LISTS EXTENSION_STATIC_LINK_LIST)
    add_static_link_extension(${extension})
endforeach()

