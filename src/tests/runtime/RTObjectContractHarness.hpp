//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTObjectContractHarness.hpp
// Purpose: Shared fake rt_object ABI for isolated runtime contract tests.
//
// Isolated contract tests compile one runtime source file directly so they can
// stub graphics, font, or platform calls. Any such test that stubs rt_obj_new_i64
// must also preserve the class_id and expose rt_obj_class_id, otherwise checked
// runtime handles link or behave differently from the real runtime.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace viper_contract {

struct ObjHeader {
    int64_t class_id;
    int32_t refcount;
    void (*finalizer)(void *);
};

inline ObjHeader *object_header_from_payload(void *obj) {
    return reinterpret_cast<ObjHeader *>(obj) - 1;
}

} // namespace viper_contract

extern "C" void *rt_obj_new_i64(int64_t class_id, int64_t byte_size) {
    assert(byte_size >= 0);
    auto *header = static_cast<viper_contract::ObjHeader *>(
        std::calloc(1, sizeof(viper_contract::ObjHeader) + static_cast<size_t>(byte_size)));
    assert(header != nullptr);
    header->class_id = class_id;
    header->refcount = 1;
    return header + 1;
}

extern "C" int64_t rt_obj_class_id(void *obj) {
    return obj ? viper_contract::object_header_from_payload(obj)->class_id : 0;
}

extern "C" void rt_obj_set_finalizer(void *obj, void (*finalizer)(void *)) {
    if (!obj)
        return;
    viper_contract::object_header_from_payload(obj)->finalizer = finalizer;
}

extern "C" void rt_obj_retain_maybe(void *obj) {
    if (!obj)
        return;
    viper_contract::object_header_from_payload(obj)->refcount++;
}

extern "C" int32_t rt_obj_release_check0(void *obj) {
    if (!obj)
        return 0;
    auto *header = viper_contract::object_header_from_payload(obj);
    assert(header->refcount > 0);
    header->refcount--;
    return header->refcount == 0;
}

extern "C" void rt_obj_free(void *obj) {
    if (!obj)
        return;
    auto *header = viper_contract::object_header_from_payload(obj);
    if (header->finalizer)
        header->finalizer(obj);
    std::free(header);
}
