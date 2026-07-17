//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTMathHandleValidationTests.cpp
// Purpose: Ensure every math-class public entry point rejects wrong-typed
//   handles with a trap instead of reading or writing through the raw cast.
// Key invariants:
//   - A handle of one math class passed to another class's API traps.
//   - Setters must not write through a wrong-typed handle (no corruption).
//   - Valid handles continue to work with zero traps.
// Ownership/Lifetime: Overrides vm_trap to observe trap payloads.
// Links: src/runtime/graphics/math/rt_vec2.c, rt_vec3.c, rt_quat.c, rt_mat3.c,
//        rt_spline.c
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_mat3.h"
#include "rt_quat.h"
#include "rt_seq.h"
#include "rt_spline.h"
#include "rt_vec2.h"
#include "rt_vec3.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
int g_trap_count = 0;
std::string g_last_trap;

extern "C" void vm_trap(const char *msg) {
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

void reset_traps() {
    g_trap_count = 0;
    g_last_trap.clear();
}
} // namespace

int main() {
    void *v3 = rt_vec3_new(1.0, 2.0, 3.0);
    void *v2 = rt_vec2_new(1.0, 2.0);
    void *q = rt_quat_new(0.0, 0.0, 0.0, 1.0);
    assert(v3 && v2 && q);

    /* Vec3 setters must trap on a wrong-typed handle, not write through it. */
    reset_traps();
    rt_vec3_set_x(q, 99.0);
    assert(g_trap_count == 1);
    assert(g_last_trap == "Vec3.set_X: invalid vector");
    assert(std::fabs(rt_quat_x(q)) < 1e-12); /* quat payload untouched */

    reset_traps();
    rt_vec3_set(v2, 9.0, 9.0, 9.0); /* Vec2 payload is smaller than Vec3 */
    assert(g_trap_count == 1);

    reset_traps();
    rt_vec3_copy_from(v2, v3);
    assert(g_trap_count >= 1);

    /* Vec3 arithmetic rejects wrong-typed operands. */
    reset_traps();
    assert(rt_vec3_add(v3, q) == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap == "Vec3.Add: invalid vector");

    /* Quat operations reject a Vec3 (24-byte payload would read OOB). */
    reset_traps();
    assert(rt_quat_mul(v3, v3) == NULL);
    assert(g_trap_count >= 1);
    assert(g_last_trap == "Quat.Mul: invalid quaternion");

    reset_traps();
    assert(rt_quat_inverse(v3) == NULL);
    assert(g_trap_count == 1);

    reset_traps();
    assert(rt_quat_rotate_vec3(v3, v3) == NULL);
    assert(g_trap_count == 1);

    /* Spline entry points reject foreign handles before dereferencing the
     * internal coordinate-array pointers. */
    reset_traps();
    assert(rt_spline_eval(v3, 0.5) == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap == "Spline.Eval: invalid spline");

    reset_traps();
    assert(rt_spline_point_count(q) == 0);
    assert(g_trap_count == 1);

    /* Vec2 arithmetic rejects wrong-typed operands. */
    reset_traps();
    assert(rt_vec2_add(v3, v3) == NULL);
    assert(g_trap_count >= 1);

    /* Mat3 traps on wrong-typed (non-NULL) handles even on soft-null APIs. */
    reset_traps();
    (void)rt_mat3_add(v3, v3);
    assert(g_trap_count >= 1);

    /* Valid handles keep working with zero traps. */
    reset_traps();
    void *sum = rt_vec3_add(v3, v3);
    assert(sum != NULL);
    assert(std::fabs(rt_vec3_x(sum) - 2.0) < 1e-12);
    void *qi = rt_quat_inverse(q);
    assert(qi != NULL);
    void *m3 = rt_mat3_identity();
    void *m3t = rt_mat3_transpose(m3);
    assert(m3t != NULL);
    assert(g_trap_count == 0);

    /* A real spline still evaluates cleanly. */
    void *pts = rt_seq_new();
    rt_seq_push(pts, rt_vec2_new(0.0, 0.0));
    rt_seq_push(pts, rt_vec2_new(1.0, 1.0));
    void *spline = rt_spline_linear(pts);
    assert(spline != NULL);
    void *mid = rt_spline_eval(spline, 0.5);
    assert(mid != NULL);
    assert(std::fabs(rt_vec2_x(mid) - 0.5) < 1e-9);
    assert(g_trap_count == 0);

    printf("RTMathHandleValidationTests: all checks passed\n");
    return 0;
}
