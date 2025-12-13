//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMapTests.cpp
// Purpose: Tests for Viper.Collections.Map runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_string.h"

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

static rt_string make_key(const char *text)
{
    assert(text != nullptr);
    return rt_string_from_bytes(text, strlen(text));
}

static void test_remove_frees_last_reference_without_invalid_free()
{
    void *map = rt_map_new();
    assert(map != nullptr);

    rt_string key = make_key("k");
    void *value = new_obj();

    rt_map_set(map, key, value);
    // Drop the creator reference; map now owns the single remaining ref.
    rt_release_obj(value);

    assert(rt_map_len(map) == 1);
    assert(rt_map_remove(map, key) == 1);
    assert(rt_map_len(map) == 0);

    rt_string_unref(key);
    rt_release_obj(map);
}

static void test_overwrite_frees_old_last_reference_without_invalid_free()
{
    void *map = rt_map_new();
    assert(map != nullptr);

    rt_string key = make_key("k");
    void *value1 = new_obj();
    void *value2 = new_obj();

    rt_map_set(map, key, value1);
    rt_release_obj(value1);

    // Overwrite should release+free the old value when it was the last reference.
    rt_map_set(map, key, value2);
    rt_release_obj(value2);

    assert(rt_map_len(map) == 1);
    assert(rt_map_remove(map, key) == 1);
    assert(rt_map_len(map) == 0);

    rt_string_unref(key);
    rt_release_obj(map);
}

int main()
{
    test_remove_frees_last_reference_without_invalid_free();
    test_overwrite_frees_old_last_reference_without_invalid_free();
    return 0;
}

