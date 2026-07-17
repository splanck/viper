//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMatEqTests.cpp
// Purpose: VDOC-207 regression — Mat3.Eq / Mat4.Eq must reject NaN components
//   and a non-finite epsilon instead of reporting arbitrary matrices as equal.
//
//===----------------------------------------------------------------------===//

#include "rt_mat3.h"
#include "rt_mat4.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// Record-and-return trap hook: lets us observe that a singular inverse trapped
// (and returned NULL) without aborting the test process.
static const char *g_last_trap = nullptr;
extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
}

int main() {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // --- Mat3 ---
    void *m3_zero = rt_mat3_zero();
    void *m3_id = rt_mat3_identity();
    // A NaN epsilon must not make everything "equal".
    assert(rt_mat3_eq(m3_zero, m3_id, nan) == 0);
    // A finite epsilon: zero != identity.
    assert(rt_mat3_eq(m3_zero, m3_id, 1e-9) == 0);
    // Equal matrices are equal.
    assert(rt_mat3_eq(m3_zero, rt_mat3_zero(), 1e-9) == 1);
    // A NaN component makes a matrix unequal to zero — even with a huge epsilon.
    void *m3_nan = rt_mat3_new(nan, 0, 0, 0, 0, 0, 0, 0, 0);
    assert(rt_mat3_eq(m3_nan, rt_mat3_zero(), 1e6) == 0);
    // A matrix with NaN is not even equal to itself (IEEE semantics).
    assert(rt_mat3_eq(m3_nan, m3_nan, 1e-9) == 0);

    // --- Mat4 ---
    void *m4_zero = rt_mat4_zero();
    void *m4_id = rt_mat4_identity();
    assert(rt_mat4_eq(m4_zero, m4_id, nan) == 0); // VDOC-207 headline case
    assert(rt_mat4_eq(m4_zero, m4_id, 1e-9) == 0);
    assert(rt_mat4_eq(m4_zero, rt_mat4_zero(), 1e-9) == 1);
    void *m4_nan = rt_mat4_new(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert(rt_mat4_eq(m4_nan, rt_mat4_zero(), 1e6) == 0);
    assert(rt_mat4_eq(m4_nan, m4_nan, 1e-9) == 0);

    // Signed zero compares equal (-0.0 == 0.0).
    void *m4_negzero = rt_mat4_new(-0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert(rt_mat4_eq(m4_negzero, rt_mat4_zero(), 1e-9) == 1);

    // VDOC-208: Mat3.Inverse and Mat4.Inverse have ONE failure contract — both
    // trap on a singular matrix instead of Mat4 silently returning identity.
    g_last_trap = nullptr;
    void *m3_sing_inv = rt_mat3_inverse(rt_mat3_zero());
    assert(m3_sing_inv == nullptr);
    assert(g_last_trap != nullptr && std::strstr(g_last_trap, "singular") != nullptr);

    g_last_trap = nullptr;
    void *m4_sing_inv = rt_mat4_inverse(rt_mat4_zero());
    assert(m4_sing_inv == nullptr); // was a bogus identity before the fix
    assert(g_last_trap != nullptr && std::strstr(g_last_trap, "singular") != nullptr);

    // An invertible matrix still inverts (identity inverse is identity).
    void *m4_id_inv = rt_mat4_inverse(rt_mat4_identity());
    assert(m4_id_inv != nullptr);
    assert(rt_mat4_eq(m4_id_inv, rt_mat4_identity(), 1e-9) == 1);

    // The non-trapping internal variant returns NULL (no trap) for a singular
    // matrix so engine callers can degrade gracefully.
    g_last_trap = nullptr;
    void *m4_try = rt_mat4_try_inverse(rt_mat4_zero());
    assert(m4_try == nullptr);
    assert(g_last_trap == nullptr);

    std::printf("RTMatEqTests: all passed\n");
    return 0;
}
