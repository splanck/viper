//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCoreOwnershipTests.cpp
// Purpose: Validate core runtime ownership rules for copied C strings,
//          borrowed native pointers, and Option/Result retention.
//
//===----------------------------------------------------------------------===//

#include "rt_object.h"
#include "rt_option.h"
#include "rt_result.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

static int g_finalizer_count = 0;

static void release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void count_finalizer(void *obj) {
    (void)obj;
    ++g_finalizer_count;
}

static void test_const_cstr_copies_transient_input(void) {
    char buffer[] = "alpha";
    rt_string s = rt_const_cstr(buffer);
    assert(s != nullptr);
    buffer[0] = 'o';
    assert(strcmp(rt_string_cstr(s), "alpha") == 0);
    rt_string_unref(s);
}

static void test_foreign_pointers_are_borrowed(void) {
    int local = 42;
    assert(rt_obj_class_id(&local) == 0);
    rt_obj_retain_maybe(&local);
    assert(rt_obj_release_check0(&local) == 0);
    rt_obj_free(&local);

    void *opt = rt_option_some(&local);
    assert(opt != nullptr);
    release_object(opt);

    void *res = rt_result_err(&local);
    assert(res != nullptr);
    release_object(res);
}

static void test_option_retains_runtime_objects(void) {
    g_finalizer_count = 0;
    void *payload = rt_obj_new_i64(0xC0DE, (int64_t)sizeof(int64_t));
    assert(payload != nullptr);
    rt_obj_set_finalizer(payload, count_finalizer);

    void *opt = rt_option_some(payload);
    assert(opt != nullptr);
    assert(rt_obj_release_check0(payload) == 0);
    assert(g_finalizer_count == 0);

    release_object(opt);
    assert(g_finalizer_count == 1);
}

static void test_result_retains_runtime_objects(void) {
    g_finalizer_count = 0;
    void *payload = rt_obj_new_i64(0xC0DF, (int64_t)sizeof(int64_t));
    assert(payload != nullptr);
    rt_obj_set_finalizer(payload, count_finalizer);

    void *res = rt_result_ok(payload);
    assert(res != nullptr);
    assert(rt_obj_release_check0(payload) == 0);
    assert(g_finalizer_count == 0);

    release_object(res);
    assert(g_finalizer_count == 1);
}

} // namespace

int main() {
    test_const_cstr_copies_transient_input();
    test_foreign_pointers_are_borrowed();
    test_option_retains_runtime_objects();
    test_result_retains_runtime_objects();
    printf("RTCoreOwnershipTests passed.\n");
    return 0;
}
