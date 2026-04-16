//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_particles3d.h"

#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::jmp_buf g_env;
const char *g_last_trap = nullptr;
bool g_expect_trap = false;

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" void rt_obj_retain_maybe(void *) {}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" void rt_canvas3d_add_temp_buffer(void *, void *) {}

extern "C" void *rt_material3d_new(void) {
    return std::calloc(1, 1);
}

extern "C" void rt_material3d_set_color(void *, double, double, double) {}

extern "C" void rt_material3d_set_unlit(void *, int8_t) {}

extern "C" void rt_material3d_set_alpha(void *, double) {}

extern "C" void rt_material3d_set_texture(void *, void *) {}

extern "C" void *rt_mat4_identity(void) {
    static double identity[16] = {
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    return identity;
}

extern "C" void rt_canvas3d_draw_mesh(void *, void *, void *, void *) {}

extern "C" void rt_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_env, 1);
    std::abort();
}

static void expect_trap_on_invalid_capacity() {
    g_expect_trap = true;
    if (setjmp(g_env) == 0) {
        (void)rt_particles3d_new(0);
        assert(false && "expected rt_trap");
    }
    g_expect_trap = false;
    assert(g_last_trap != nullptr);
    assert(std::strstr(g_last_trap, "max_particles") != nullptr);
}

static void test_burst_and_clear() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);
    assert(rt_particles3d_get_count(ps) == 0);
    assert(rt_particles3d_get_emitting(ps) == 0);

    rt_particles3d_burst(ps, 3);
    assert(rt_particles3d_get_count(ps) == 3);

    rt_particles3d_clear(ps);
    assert(rt_particles3d_get_count(ps) == 0);
}

static void test_start_stop_and_update_spawns_particles() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);

    rt_particles3d_set_rate(ps, 4.0);
    rt_particles3d_start(ps);
    assert(rt_particles3d_get_emitting(ps) == 1);

    rt_particles3d_update(ps, 0.5);
    assert(rt_particles3d_get_count(ps) == 2);

    rt_particles3d_stop(ps);
    assert(rt_particles3d_get_emitting(ps) == 0);
    int64_t count_after_stop = rt_particles3d_get_count(ps);
    rt_particles3d_update(ps, 0.5);
    assert(rt_particles3d_get_count(ps) <= count_after_stop);
}

static void test_particles_expire_after_lifetime() {
    void *ps = rt_particles3d_new(8);
    assert(ps != nullptr);

    rt_particles3d_set_lifetime(ps, 0.1, 0.1);
    rt_particles3d_burst(ps, 4);
    assert(rt_particles3d_get_count(ps) == 4);

    rt_particles3d_update(ps, 0.2);
    assert(rt_particles3d_get_count(ps) == 0);
}

int main() {
    expect_trap_on_invalid_capacity();
    test_burst_and_clear();
    test_start_stop_and_update_spawns_particles();
    test_particles_expire_after_lifetime();
    std::printf("RTParticles3DContractTests passed.\n");
    return 0;
}
