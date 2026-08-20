#include "koredb_arrow.h"

namespace koredb_arrow {

ArrowSchema query_result_get_arrow_schema(const koredb::main::QueryResult& result) {
    // Could use directly, except that we can't (yet) mark ArrowSchema as being safe to store in a
    // cxx::UniquePtr
    return *result.getArrowSchema();
}

ArrowArray query_result_get_next_arrow_chunk(koredb::main::QueryResult& result,
    uint64_t chunkSize) {
    return *result.getNextArrowChunk(chunkSize);
}

} // namespace koredb_arrow
