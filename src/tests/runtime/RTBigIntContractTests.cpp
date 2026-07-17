//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBigIntContractTests.cpp
// Purpose: Validate BigInt edge semantics for parsing, narrowing, and traps.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_bigint.h"
#include "rt_seq.h"
#include "rt_error.h"
#include "rt_string.h"

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace {

static void release_bigint(void *bi) {
    if (bi && rt_obj_release_check0(bi))
        rt_obj_free(bi);
}

static void expect_trap(void (*fn)(),
                        int64_t expected_kind,
                        int64_t expected_code,
                        const char *snippet) {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == expected_kind);
    assert(rt_trap_get_code() == expected_code);
    assert(message != nullptr);
    assert(strstr(message, snippet) != nullptr);
    rt_trap_clear_recovery();
}

static void trap_invalid_base() {
    void *bi = rt_bigint_from_i64(7);
    assert(bi != nullptr);
    (void)rt_bigint_to_str_base(bi, 1);
}

// VDOC-204: a non-BigInt object passed through the generic `obj` signature is
// rejected at the boundary instead of being dereferenced as a bigint_t.
static void trap_wrong_class_sign() {
    void *seq = rt_seq_new();
    (void)rt_bigint_sign(seq);
}

static void trap_wrong_class_add() {
    void *seq = rt_seq_new();
    void *bi = rt_bigint_from_i64(1);
    (void)rt_bigint_add(bi, seq);
}

static void test_saturating_narrowing(void) {
    void *pos = rt_bigint_from_str(rt_const_cstr("9223372036854775808"));
    assert(pos != nullptr);
    assert(rt_bigint_fits_i64(pos) == 0);
    assert(rt_bigint_to_i64(pos) == INT64_MAX);
    release_bigint(pos);

    void *neg = rt_bigint_from_str(rt_const_cstr("-9223372036854775809"));
    assert(neg != nullptr);
    assert(rt_bigint_fits_i64(neg) == 0);
    assert(rt_bigint_to_i64(neg) == INT64_MIN);
    release_bigint(neg);
}

static void test_invalid_parse_rejected(void) {
    assert(rt_bigint_from_str(rt_const_cstr("123abc")) == nullptr);
    assert(rt_bigint_from_str(rt_const_cstr("0x")) == nullptr);
    assert(rt_bigint_from_str(rt_const_cstr("   ")) == nullptr);
}

// VDOC-205: PowMod returns the least non-negative residue in [0, |m|), even for
// a negative base — Mod's truncating semantics would otherwise leak a negative
// residue.
static void test_pow_mod_nonnegative_residue(void) {
    void *a = rt_bigint_from_i64(-2);
    void *n = rt_bigint_from_i64(3);
    void *m = rt_bigint_from_i64(5);
    void *r = rt_bigint_pow_mod(a, n, m);
    assert(r != nullptr);
    assert(rt_bigint_to_i64(r) == 2); // -8 mod 5 == 2 (was -3)
    release_bigint(a);
    release_bigint(n);
    release_bigint(m);
    release_bigint(r);

    // A positive base is unchanged: 2^10 mod 1000 == 24.
    void *a2 = rt_bigint_from_i64(2);
    void *n2 = rt_bigint_from_i64(10);
    void *m2 = rt_bigint_from_i64(1000);
    void *r2 = rt_bigint_pow_mod(a2, n2, m2);
    assert(r2 != nullptr);
    assert(rt_bigint_to_i64(r2) == 24);
    release_bigint(a2);
    release_bigint(n2);
    release_bigint(m2);
    release_bigint(r2);
}

} // namespace

int main() {
    test_saturating_narrowing();
    test_invalid_parse_rejected();
    test_pow_mod_nonnegative_residue();
    expect_trap(trap_invalid_base, RT_TRAP_KIND_DOMAIN_ERROR, 0, "base must be between 2 and 36");
    expect_trap(trap_wrong_class_sign, RT_TRAP_KIND_DOMAIN_ERROR, 0, "invalid BigInt object");
    expect_trap(trap_wrong_class_add, RT_TRAP_KIND_DOMAIN_ERROR, 0, "invalid BigInt object");
    printf("RTBigIntContractTests passed.\n");
    return 0;
}
