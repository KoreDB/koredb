add_library(koredb_storage_wal
        OBJECT
        local_wal.cpp
        wal.cpp
        wal_record.cpp
        wal_replayer.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_storage_wal>
        PARENT_SCOPE)
