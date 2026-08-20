add_library(koredb_llm_function
        OBJECT
        create_embedding.cpp)

set(LLM_EXTENSION_OBJECT_FILES
        ${LLM_EXTENSION_OBJECT_FILES} $<TARGET_OBJECTS:koredb_llm_function>
        PARENT_SCOPE)
