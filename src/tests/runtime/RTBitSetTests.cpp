//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBitSetTests.cpp
// Purpose: Tests for Viper.Collections.BitSet runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_bitset.h"
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

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_new_bitset()
{
    void *bs = rt_bitset_new(128);
    assert(bs != nullptr);
    assert(rt_bitset_len(bs) == 128);
    assert(rt_bitset_count(bs) == 0);
    assert(rt_bitset_is_empty(bs) == 1);
    rt_release_obj(bs);
}

static void test_set_and_get()
{
    void *bs = rt_bitset_new(64);

    rt_bitset_set(bs, 0);
    rt_bitset_set(bs, 5);
    rt_bitset_set(bs, 63);

    assert(rt_bitset_get(bs, 0) == 1);
    assert(rt_bitset_get(bs, 1) == 0);
    assert(rt_bitset_get(bs, 5) == 1);
    assert(rt_bitset_get(bs, 63) == 1);
    assert(rt_bitset_count(bs) == 3);
    assert(rt_bitset_is_empty(bs) == 0);

    rt_release_obj(bs);
}

static void test_clear_bit()
{
    void *bs = rt_bitset_new(64);

    rt_bitset_set(bs, 10);
    assert(rt_bitset_get(bs, 10) == 1);

    rt_bitset_clear(bs, 10);
    assert(rt_bitset_get(bs, 10) == 0);
    assert(rt_bitset_count(bs) == 0);

    rt_release_obj(bs);
}

static void test_toggle()
{
    void *bs = rt_bitset_new(64);

    rt_bitset_toggle(bs, 7);
    assert(rt_bitset_get(bs, 7) == 1);

    rt_bitset_toggle(bs, 7);
    assert(rt_bitset_get(bs, 7) == 0);

    rt_release_obj(bs);
}

static void test_set_all_and_clear_all()
{
    void *bs = rt_bitset_new(10);

    rt_bitset_set_all(bs);
    assert(rt_bitset_count(bs) == 10);
    for (int i = 0; i < 10; ++i)
        assert(rt_bitset_get(bs, i) == 1);
    // Bits beyond bit_count should not be set
    // (last word is masked)

    rt_bitset_clear_all(bs);
    assert(rt_bitset_count(bs) == 0);
    for (int i = 0; i < 10; ++i)
        assert(rt_bitset_get(bs, i) == 0);

    rt_release_obj(bs);
}

static void test_auto_grow()
{
    void *bs = rt_bitset_new(8);
    assert(rt_bitset_len(bs) == 8);

    // Setting a bit beyond the current capacity should grow
    rt_bitset_set(bs, 200);
    assert(rt_bitset_len(bs) == 201);
    assert(rt_bitset_get(bs, 200) == 1);
    assert(rt_bitset_count(bs) == 1);

    rt_release_obj(bs);
}

static void test_and()
{
    void *a = rt_bitset_new(8);
    void *b = rt_bitset_new(8);

    rt_bitset_set(a, 0);
    rt_bitset_set(a, 1);
    rt_bitset_set(a, 2);

    rt_bitset_set(b, 1);
    rt_bitset_set(b, 2);
    rt_bitset_set(b, 3);

    void *result = rt_bitset_and(a, b);
    // AND: bits 1,2 should be set
    assert(rt_bitset_get(result, 0) == 0);
    assert(rt_bitset_get(result, 1) == 1);
    assert(rt_bitset_get(result, 2) == 1);
    assert(rt_bitset_get(result, 3) == 0);
    assert(rt_bitset_count(result) == 2);

    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(result);
}

static void test_or()
{
    void *a = rt_bitset_new(8);
    void *b = rt_bitset_new(8);

    rt_bitset_set(a, 0);
    rt_bitset_set(a, 2);

    rt_bitset_set(b, 1);
    rt_bitset_set(b, 2);

    void *result = rt_bitset_or(a, b);
    // OR: bits 0,1,2 should be set
    assert(rt_bitset_get(result, 0) == 1);
    assert(rt_bitset_get(result, 1) == 1);
    assert(rt_bitset_get(result, 2) == 1);
    assert(rt_bitset_get(result, 3) == 0);
    assert(rt_bitset_count(result) == 3);

    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(result);
}

static void test_xor()
{
    void *a = rt_bitset_new(8);
    void *b = rt_bitset_new(8);

    rt_bitset_set(a, 0);
    rt_bitset_set(a, 1);

    rt_bitset_set(b, 1);
    rt_bitset_set(b, 2);

    void *result = rt_bitset_xor(a, b);
    // XOR: bits 0,2 should be set
    assert(rt_bitset_get(result, 0) == 1);
    assert(rt_bitset_get(result, 1) == 0);
    assert(rt_bitset_get(result, 2) == 1);
    assert(rt_bitset_count(result) == 2);

    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(result);
}

static void test_not()
{
    void *bs = rt_bitset_new(4);
    rt_bitset_set(bs, 0);
    rt_bitset_set(bs, 2);

    void *result = rt_bitset_not(bs);
    // NOT: bits 1,3 should be set (within 4-bit range)
    assert(rt_bitset_get(result, 0) == 0);
    assert(rt_bitset_get(result, 1) == 1);
    assert(rt_bitset_get(result, 2) == 0);
    assert(rt_bitset_get(result, 3) == 1);
    assert(rt_bitset_count(result) == 2);

    rt_release_obj(bs);
    rt_release_obj(result);
}

static void test_to_string()
{
    void *bs = rt_bitset_new(8);
    rt_bitset_set(bs, 0); // bit 0 = LSB
    rt_bitset_set(bs, 2);
    rt_bitset_set(bs, 4);
    rt_bitset_set(bs, 7);

    // Binary: 10010101 (MSB first)
    rt_string s = rt_bitset_to_string(bs);
    assert(str_eq(s, "10010101"));
    rt_string_unref(s);

    rt_release_obj(bs);
}

static void test_to_string_empty()
{
    void *bs = rt_bitset_new(8);
    rt_string s = rt_bitset_to_string(bs);
    assert(str_eq(s, "0"));
    rt_string_unref(s);

    rt_release_obj(bs);
}

static void test_large_bitset()
{
    void *bs = rt_bitset_new(1000);

    rt_bitset_set(bs, 0);
    rt_bitset_set(bs, 500);
    rt_bitset_set(bs, 999);

    assert(rt_bitset_count(bs) == 3);
    assert(rt_bitset_get(bs, 0) == 1);
    assert(rt_bitset_get(bs, 500) == 1);
    assert(rt_bitset_get(bs, 999) == 1);
    assert(rt_bitset_get(bs, 501) == 0);

    rt_release_obj(bs);
}

static void test_different_sizes_or()
{
    void *a = rt_bitset_new(8);
    void *b = rt_bitset_new(128);

    rt_bitset_set(a, 3);
    rt_bitset_set(b, 100);

    void *result = rt_bitset_or(a, b);
    assert(rt_bitset_get(result, 3) == 1);
    assert(rt_bitset_get(result, 100) == 1);
    assert(rt_bitset_count(result) == 2);

    rt_release_obj(a);
    rt_release_obj(b);
    rt_release_obj(result);
}

static void test_null_safety()
{
    assert(rt_bitset_len(NULL) == 0);
    assert(rt_bitset_count(NULL) == 0);
    assert(rt_bitset_is_empty(NULL) == 1);
    assert(rt_bitset_get(NULL, 0) == 0);
    rt_bitset_set(NULL, 0);      // No-op
    rt_bitset_clear(NULL, 0);    // No-op
    rt_bitset_toggle(NULL, 0);   // No-op
    rt_bitset_set_all(NULL);     // No-op
    rt_bitset_clear_all(NULL);   // No-op
}

static void test_negative_index()
{
    void *bs = rt_bitset_new(64);
    // Negative indices should be handled gracefully
    assert(rt_bitset_get(bs, -1) == 0);
    rt_bitset_set(bs, -5);   // No-op
    rt_bitset_clear(bs, -1); // No-op
    assert(rt_bitset_count(bs) == 0);
    rt_release_obj(bs);
}

int main()
{
    test_new_bitset();
    test_set_and_get();
    test_clear_bit();
    test_toggle();
    test_set_all_and_clear_all();
    test_auto_grow();
    test_and();
    test_or();
    test_xor();
    test_not();
    test_to_string();
    test_to_string_empty();
    test_large_bitset();
    test_different_sizes_or();
    test_null_safety();
    test_negative_index();
    return 0;
}
