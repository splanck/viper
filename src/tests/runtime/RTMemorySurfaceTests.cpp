//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMemorySurfaceTests.cpp
// Purpose: Validate the public Viper.Memory retain/release surface and heap
//          correctness traps that protect it.
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "rt_heap.h"
#include "rt_object.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <string>

namespace {

int g_finalizer_count = 0;
void *g_resurrected = nullptr;

void count_finalizer(void *obj) {
    (void)obj;
    g_finalizer_count++;
}

void resurrect_finalizer(void *obj) {
    g_resurrected = obj;
    rt_obj_resurrect(obj);
}

void call_memory_retain_invalid() {
    int local = 1;
    rt_memory_retain(&local);
}

void call_memory_release_invalid() {
    int local = 1;
    rt_memory_release(&local);
}

void call_object_negative_size() {
    rt_obj_new_i64(7, -1);
}

void call_heap_double_release_deferred() {
    void *obj = rt_obj_new_i64(8, 8);
    assert(rt_heap_release_deferred(obj) == 0);
    rt_heap_release_deferred(obj);
}

void call_heap_retain_overflow() {
    void *obj = rt_obj_new_i64(9, 8);
    rt_heap_hdr_t *hdr = rt_heap_hdr(obj);
    __atomic_store_n(&hdr->refcnt, SIZE_MAX - 1, __ATOMIC_RELAXED);
    rt_heap_retain(obj);
}

void call_heap_set_len_past_capacity() {
    void *payload = rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_U8, 1, 0, 4);
    assert(payload != nullptr);
    rt_heap_set_len(payload, 5);
}

void call_resurrect_live_object() {
    void *obj = rt_obj_new_i64(10, 8);
    rt_obj_resurrect(obj);
}

void expect_trap(void (*fn)(), const char *message) {
    auto result = viper::tests::runIsolated(fn);
    assert(result.trapped());
    assert(result.stderrText.find(message) != std::string::npos);
}

void test_memory_release_runs_finalizer() {
    g_finalizer_count = 0;
    void *obj = rt_obj_new_i64(0xCAFE, 16);
    assert(obj != nullptr);
    rt_obj_set_finalizer(obj, count_finalizer);

    rt_memory_retain(obj);
    assert(rt_memory_release(obj) == 1);
    assert(g_finalizer_count == 0);
    assert(rt_heap_is_payload(obj) == 1);

    assert(rt_memory_release(obj) == 0);
    assert(g_finalizer_count == 1);
    assert(rt_heap_is_payload(obj) == 0);
}

void test_memory_release_reports_string_refcount() {
    rt_string s = rt_string_from_bytes("refcounted string", 17);
    assert(s != nullptr);
    rt_memory_retain(s);
    assert(rt_memory_release(s) == 1);
    assert(rt_memory_release(s) == 0);

    rt_string small = rt_string_from_bytes("sso", 3);
    assert(small != nullptr);
    rt_memory_retain(small);
    assert(rt_memory_release(small) == 1);
    assert(rt_memory_release(small) == 0);
}

void test_memory_release_reports_resurrection_refcount() {
    g_resurrected = nullptr;
    void *obj = rt_obj_new_i64(0x515, 16);
    assert(obj != nullptr);
    rt_obj_set_finalizer(obj, resurrect_finalizer);

    assert(rt_memory_release(obj) == 1);
    assert(g_resurrected == obj);
    assert(rt_heap_is_payload(obj) == 1);
    assert(rt_memory_release(obj) == 0);
    assert(rt_heap_is_payload(obj) == 0);
}

void test_memory_release_array_drops_elements() {
    g_finalizer_count = 0;
    void *obj = rt_obj_new_i64(0xBEEF, 8);
    rt_obj_set_finalizer(obj, count_finalizer);

    void **arr = (void **)rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_NONE, sizeof(void *), 1, 1);
    assert(arr != nullptr);
    arr[0] = obj;
    rt_obj_retain_maybe(obj);

    assert(rt_obj_release_check0(obj) == 0);
    assert(g_finalizer_count == 0);

    assert(rt_memory_release(arr) == 0);
    assert(g_finalizer_count == 1);
    assert(rt_heap_is_payload(arr) == 0);
}

void test_heap_mark_disposed_return_contract() {
    void *obj = rt_obj_new_i64(0xD15, 8);
    assert(rt_heap_mark_disposed(obj) == 1);
    assert(rt_heap_mark_disposed(obj) == 0);
    assert(rt_obj_release_check0(obj) == 1);
    rt_obj_free(obj);
}

} // namespace

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(call_memory_retain_invalid);
    viper::tests::registerChildFunction(call_memory_release_invalid);
    viper::tests::registerChildFunction(call_object_negative_size);
    viper::tests::registerChildFunction(call_heap_double_release_deferred);
    viper::tests::registerChildFunction(call_heap_retain_overflow);
    viper::tests::registerChildFunction(call_heap_set_len_past_capacity);
    viper::tests::registerChildFunction(call_resurrect_live_object);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    test_memory_release_runs_finalizer();
    test_memory_release_reports_string_refcount();
    test_memory_release_reports_resurrection_refcount();
    test_memory_release_array_drops_elements();
    test_heap_mark_disposed_return_contract();
    expect_trap(call_memory_retain_invalid, "Viper.Memory.Retain");
    expect_trap(call_memory_release_invalid, "Viper.Memory.Release");
    expect_trap(call_object_negative_size, "negative object size");
    expect_trap(call_heap_double_release_deferred, "double release");
    expect_trap(call_heap_retain_overflow, "refcount overflow");
    expect_trap(call_heap_set_len_past_capacity, "length exceeds capacity");
    expect_trap(call_resurrect_live_object, "refcount is not zero");
    return 0;
}
