#include "include/cached_import/py_cached_import.h"
#include "include/py_connection.h"
#include "include/py_database.h"
#include "include/py_prepared_statement.h"
#include "include/py_query_result.h"

void bind(py::module& m) {
    PyDatabase::initialize(m);
    PyConnection::initialize(m);
    PyPreparedStatement::initialize(m);
    PyQueryResult::initialize(m);
    auto cleanImportCache = [](void*) noexcept { koredb::importCache.reset(); };
    m.attr("_clean_import_cache") = py::capsule(&koredb::importCache, cleanImportCache);
}

NB_MODULE(_koredb, m) {
    bind(m);
}
