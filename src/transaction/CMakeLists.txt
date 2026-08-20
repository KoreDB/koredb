add_library(koredb_transaction
        OBJECT
        transaction.cpp
        transaction_context.cpp
        transaction_manager.cpp)

set(ALL_OBJECT_FILES
        ${ALL_OBJECT_FILES} $<TARGET_OBJECTS:koredb_transaction>
        PARENT_SCOPE)
