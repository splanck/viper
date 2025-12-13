//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTListTests.cpp
// Purpose: Tests for Viper.Collections.List runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_list.h"
#include "rt_object.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void rt_release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static void cleanup_list(void *list)
{
    if (!list)
        return;
    rt_list_clear(list);
    rt_release_obj(list);
}

static void assert_item(void *list, int64_t index, void *expected)
{
    void *got = rt_list_get_item(list, index);
    assert(got == expected);
    rt_release_obj(got);
}

static void test_has_empty_and_nonempty()
{
    void *list = rt_ns_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();

    assert(rt_list_get_count(list) == 0);
    assert(rt_list_has(list, a) == 0);

    rt_list_add(list, a);
    assert(rt_list_get_count(list) == 1);
    assert(rt_list_has(list, a) == 1);
    assert(rt_list_has(list, b) == 0);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
}

static void test_find_returns_index_or_minus1()
{
    void *list = rt_ns_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();
    void *d = new_obj();

    rt_list_add(list, a);
    rt_list_add(list, b);
    rt_list_add(list, c);

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

static void test_insert_begin_middle_end()
{
    void *list = rt_ns_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();

    rt_list_insert(list, 0, a);
    assert(rt_list_get_count(list) == 1);
    assert_item(list, 0, a);

    rt_list_insert(list, 1, c); // append (index == Count)
    assert(rt_list_get_count(list) == 2);
    assert_item(list, 0, a);
    assert_item(list, 1, c);

    rt_list_insert(list, 1, b); // middle
    assert(rt_list_get_count(list) == 3);
    assert_item(list, 0, a);
    assert_item(list, 1, b);
    assert_item(list, 2, c);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
}

static void test_remove_returns_bool_and_removes_first_only()
{
    void *list = rt_ns_list_new();
    assert(list != nullptr);

    void *a = new_obj();
    void *b = new_obj();
    void *c = new_obj();
    void *missing = new_obj();

    rt_list_add(list, a);
    rt_list_add(list, b);
    rt_list_add(list, a);
    rt_list_add(list, c);

    assert(rt_list_get_count(list) == 4);
    assert(rt_list_remove(list, missing) == 0);

    assert(rt_list_remove(list, a) == 1);
    assert(rt_list_get_count(list) == 3);
    assert_item(list, 0, b);
    assert_item(list, 1, a);
    assert_item(list, 2, c);

    cleanup_list(list);
    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(c);
    rt_release_obj(missing);
}

static void test_insert_out_of_range_traps()
{
    void *list = rt_ns_list_new();
    assert(list != nullptr);
    void *a = new_obj();

    EXPECT_TRAP(rt_list_insert(list, -1, a));
    assert(g_last_trap && strstr(g_last_trap, "List.Insert") != nullptr);

    EXPECT_TRAP(rt_list_insert(list, 1, a));
    assert(g_last_trap && strstr(g_last_trap, "List.Insert") != nullptr);

    cleanup_list(list);
    rt_release_obj(a);
}

int main()
{
    test_has_empty_and_nonempty();
    test_find_returns_index_or_minus1();
    test_insert_begin_middle_end();
    test_remove_returns_bool_and_removes_first_only();
    test_insert_out_of_range_traps();
    return 0;
}

