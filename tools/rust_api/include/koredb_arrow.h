#pragma once

#include "rust/cxx.h"
#ifdef KOREDB_BUNDLED
#include "main/koredb.h"
#else
#include <koredb.hpp>
#endif

namespace koredb_arrow {

ArrowSchema query_result_get_arrow_schema(const koredb::main::QueryResult& result);
ArrowArray query_result_get_next_arrow_chunk(koredb::main::QueryResult& result, uint64_t chunkSize);

} // namespace koredb_arrow
