#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

namespace py {
using namespace nanobind;
using module = module_;
using function = callable;
using error_already_set = python_error;
template<class T>
using array_t = ndarray<numpy, T>;
template<class T>
T reinterpret_borrow(handle h) { return borrow<T>(h); }
template<class T>
T reinterpret_steal(handle h) { return steal<T>(h); }
inline object type_of(handle h) { return steal<object>(PyObject_Type(h.ptr())); }

class array {
    object owner;
    void* ptr = nullptr;

public:
    array() = default;
    // Object-dtype arrays cannot be exported through DLPack, which nanobind's
    // ndarray uses. Keep a NumPy-owned contiguous array alive for raw scans.
    array(handle h) : owner(module_::import_("numpy").attr("ascontiguousarray")(h)) {
        object info = owner.attr("__array_interface__");
        object data = info["data"];
        ptr = reinterpret_cast<void*>(cast<uintptr_t>(data[0]));
    }
    template<class T>
    array(const T& h) : array(static_cast<handle>(h)) {}
    array(const std::string& dtype, size_t n)
        : array(module_::import_("numpy").attr("empty")(n, arg("dtype") = dtype)) {}
    void* data() const { return ptr; }
    void* mutable_data() const { return ptr; }
    const object& as_object() const { return owner; }
};
} // namespace py
