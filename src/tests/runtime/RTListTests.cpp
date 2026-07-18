//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTListTests.cpp
// Purpose: Tests for Zanna.Collections.List runtime helpers.
// Key invariants: Append grows geometrically and removal preserves non-empty
//                 capacity without changing managed element ownership.
// Ownership/Lifetime: Lists own retained elements until removal/finalization;
//                     tests release each list and any separately owned value.
// Links: src/runtime/collections/rt_list.c, docs/zannalib/collections/sequential.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_list.h"
#include "rt_object.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
static int g_finalizer_calls = 0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void rt_release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void count_finalizer(void *) {
    ++g_finalizer_calls;
}

static void cleanup_list(void *list) {
    if (!list)
        return;
    rt_list_clear(list);
    rt_release_obj(list);
}

static void assert_item(void *list, int64_t index, void *expected) {
    void *got = rt_list_get(list, index);
    assert(got == expected);
    rt_release_obj(got);
}

/// @brief White-box view of the stable two-pointer List payload used to inspect
///        backing-array capacity without adding a language-visible API solely
///        for performance regression tests.
struct ListLayout {
    void **vptr;
    void **arr;
};

static void test_geometric_growth_and_nonshrinking_removal() {
    void *list = rt_list_new();
    assert(list != nullptr);
    void *value = new_obj();

    rt_list_push(list, value);
    auto *layout = static_cast<ListLayout *>(list);
    void **first_storage = layout->arr;
    assert(first_storage != nullptr);
    assert(rt_heap_cap(first_storage) == 16);

    for (int i = 1; i < 16; ++i)
        rt_list_push(list, value);
    assert(layout->arr == first_storage);
    assert(rt_heap_cap(layout->arr) == 16);

    rt_list_push(list, value);
    void **grown_storage = layout->arr;
    assert(rt_heap_cap(grown_storage) == 32);
    assert(rt_list_len(list) == 17);

    while (rt_list_len(list) > 1)
        rt_list_remove_at(list, rt_list_len(list) - 1);
    assert(layout->arr == grown_storage);
    assert(rt_heap_cap(layout->arr) == 32);

    cleanup_list(list);
    rt_release_obj(value);
}

static void test_has_empty_and_nonempty() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();

    assert(rt_list_len(list) == 0);
    assert(rt_list_has(list, a) == 0);

    rt_list_push(list, a);
    assert(rt_list_len(list) == 1);
    assert(rt_list_has(list, a) == 1);
    assert(rt_list_has(list, b) == 0);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
}

static void test_find_returns_index_or_minus1() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();
    void *d = new_obj();

    rt_list_push(list, a);
    rt_list_push(list, b);
    rt_list_push(list, c);

    assert(rt_list_find(list, a) == 0);
    assert(rt_list_find(list, b) == 1);
    assert(rt_list_find(list, c) == 2);
    assert(rt_list_find(list, d) == -1);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
    rt_release_obj(d);
}

static void test_insert_begin_middle_end() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();

    rt_list_insert(list, 0, a);
    assert(rt_list_len(list) == 1);
    assert_item(list, 0, a);

    rt_list_insert(list, 1, c); // append (index == Count)
    assert(rt_list_len(list) == 2);
    assert_item(list, 0, a);
    assert_item(list, 1, c);

    rt_list_insert(list, 1, b); // middle
    assert(rt_list_len(list) == 3);
    assert_item(list, 0, a);
    assert_item(list, 1, b);
    assert_item(list, 2, c);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
}

static void test_push_retain_overflow_keeps_length() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    rt_list_push(list, a);

    void *value = new_obj();
    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    EXPECT_TRAP(rt_list_push(list, value));
    assert(rt_list_len(list) == 1);
    assert_item(list, 0, a);

    hdr->refcnt = 1;
    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(value);
}

static void test_insert_retain_overflow_keeps_order() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    rt_list_push(list, a);
    rt_list_push(list, b);

    void *value = new_obj();
    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    EXPECT_TRAP(rt_list_insert(list, 1, value));
    assert(rt_list_len(list) == 2);
    assert_item(list, 0, a);
    assert_item(list, 1, b);

    hdr->refcnt = 1;
    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(value);
}

static void test_remove_returns_bool_and_removes_first_only() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();
    void *missing = new_obj();

    rt_list_push(list, a);
    rt_list_push(list, b);
    rt_list_push(list, a);
    rt_list_push(list, c);

    assert(rt_list_len(list) == 4);
    assert(rt_list_remove(list, missing) == 0);

    assert(rt_list_remove(list, a) == 1);
    assert(rt_list_len(list) == 3);
    assert_item(list, 0, b);
    assert_item(list, 1, a);
    assert_item(list, 2, c);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
    rt_release_obj(missing);
}

static void test_insert_out_of_range_traps() {
    void *list = rt_list_new();
    assert(list != nullptr);
    void *a = new_obj();

    EXPECT_TRAP(rt_list_insert(list, -1, a));
    assert(g_last_trap && strstr(g_last_trap, "List.Insert") != nullptr);

    EXPECT_TRAP(rt_list_insert(list, 1, a));
    assert(g_last_trap && strstr(g_last_trap, "List.Insert") != nullptr);

    cleanup_list(list);
    rt_release_obj(a);
}

static void test_list_finalizer_releases_elements() {
    void *list = rt_list_new();
    assert(list != nullptr);

    g_finalizer_calls = 0;

    void *a = new_obj();
    rt_obj_set_finalizer(a, count_finalizer);

    rt_list_push(list, a);
    rt_release_obj(a); // list should now be the only owner
    assert(g_finalizer_calls == 0);

    // Release the list without calling Clear(): the list finalizer must release the backing
    // array, which releases contained objects.
    rt_release_obj(list);
    assert(g_finalizer_calls == 1);
}

static void test_slice_does_not_leak_temp_get_reference() {
    void *list = rt_list_new();
    assert(list != nullptr);

    g_finalizer_calls = 0;

    void *a = new_obj();
    rt_obj_set_finalizer(a, count_finalizer);
    rt_list_push(list, a);
    rt_release_obj(a); // List now owns the only reference.

    void *slice = rt_list_slice(list, 0, 1);
    cleanup_list(list);
    assert(g_finalizer_calls == 0);

    cleanup_list(slice);
    assert(g_finalizer_calls == 1);
}

static void test_clone_does_not_leak_temp_get_reference() {
    void *list = rt_list_new();
    assert(list != nullptr);

    g_finalizer_calls = 0;

    void *a = new_obj();
    rt_obj_set_finalizer(a, count_finalizer);
    rt_list_push(list, a);
    rt_release_obj(a); // List now owns the only reference.

    void *clone = rt_list_clone(list);
    cleanup_list(list);
    assert(g_finalizer_calls == 0);

    cleanup_list(clone);
    assert(g_finalizer_calls == 1);
}

static void test_is_empty() {
    void *list = rt_list_new();
    assert(list != nullptr);

    assert(rt_list_is_empty(list) == 1);
    assert(rt_list_is_empty(nullptr) == 1);

    void *a = new_obj();
    rt_list_push(list, a);
    assert(rt_list_is_empty(list) == 0);

    cleanup_list(list);
    rt_release_obj(a);
}

static void test_pop() {
    void *list = rt_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();

    rt_list_push(list, a);
    rt_list_push(list, b);
    rt_list_push(list, c);
    assert(rt_list_len(list) == 3);

    void *popped = rt_list_pop(list);
    assert(popped == c);
    assert(rt_list_len(list) == 2);

    popped = rt_list_pop(list);
    assert(popped == b);
    assert(rt_list_len(list) == 1);

    popped = rt_list_pop(list);
    assert(popped == a);
    assert(rt_list_len(list) == 0);
    assert(rt_list_is_empty(list) == 1);

    // Pop on empty list should trap
    EXPECT_TRAP(rt_list_pop(list));
    assert(g_last_trap && strstr(g_last_trap, "List.Pop") != nullptr);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
}

int main() {
    test_geometric_growth_and_nonshrinking_removal();
    test_has_empty_and_nonempty();
    test_find_returns_index_or_minus1();
    test_insert_begin_middle_end();
    test_push_retain_overflow_keeps_length();
    test_insert_retain_overflow_keeps_order();
    test_remove_returns_bool_and_removes_first_only();
    test_insert_out_of_range_traps();
    test_list_finalizer_releases_elements();
    test_slice_does_not_leak_temp_get_reference();
    test_clone_does_not_leak_temp_get_reference();
    test_is_empty();
    test_pop();
    return 0;
}
