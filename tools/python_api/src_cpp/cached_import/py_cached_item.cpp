#include "cached_import/py_cached_item.h"

#include "cached_import/py_cached_import.h"
#include "common/exception/runtime.h"

namespace koredb {

py::handle PythonCachedItem::operator()() {
#ifndef Py_LIMITED_API
    assert((bool)PyGILState_Check());
#endif
    // load if unloaded, return cached object if already loaded
    if (loaded) {
        return object;
    }
    if (parent == nullptr) {
        object = importCache->addToCache(std::move(py::module::import_(name.c_str())));
    } else {
        object = importCache->addToCache(std::move((*parent)().attr(name.c_str())));
    }
    loaded = true;
    return object;
}

} // namespace koredb
