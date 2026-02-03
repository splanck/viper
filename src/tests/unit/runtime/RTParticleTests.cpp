//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTParticleTests.cpp - Unit tests for rt_particle
//===----------------------------------------------------------------------===//

#include "rt_particle.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  %s...", #name); \
    test_##name(); \
    printf(" OK\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

TEST(create_destroy) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    ASSERT(pe != NULL);
    ASSERT(rt_particle_emitter_count(pe) == 0);
    ASSERT(rt_particle_emitter_is_emitting(pe) == 0);
    rt_particle_emitter_destroy(pe);
}

TEST(set_position) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_position(pe, 50.0, 75.0);
    ASSERT(fabs(rt_particle_emitter_x(pe) - 50.0) < 0.001);
    ASSERT(fabs(rt_particle_emitter_y(pe) - 75.0) < 0.001);
    rt_particle_emitter_destroy(pe);
}

TEST(burst) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_position(pe, 100.0, 100.0);
    rt_particle_emitter_set_lifetime(pe, 10, 20);
    rt_particle_emitter_set_velocity(pe, 1.0, 5.0, 0.0, 360.0);

    rt_particle_emitter_burst(pe, 50);
    ASSERT(rt_particle_emitter_count(pe) == 50);
    rt_particle_emitter_destroy(pe);
}

TEST(start_stop) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_rate(pe, 5.0);

    rt_particle_emitter_start(pe);
    ASSERT(rt_particle_emitter_is_emitting(pe) == 1);

    rt_particle_emitter_stop(pe);
    ASSERT(rt_particle_emitter_is_emitting(pe) == 0);
    rt_particle_emitter_destroy(pe);
}

TEST(update_lifetime) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_lifetime(pe, 5, 5);  // Exact 5 frames
    rt_particle_emitter_set_velocity(pe, 0.0, 0.0, 0.0, 0.0);

    rt_particle_emitter_burst(pe, 10);
    ASSERT(rt_particle_emitter_count(pe) == 10);

    // Update 5 times, particles should die
    for (int i = 0; i < 5; i++) {
        rt_particle_emitter_update(pe);
    }
    ASSERT(rt_particle_emitter_count(pe) == 0);
    rt_particle_emitter_destroy(pe);
}

TEST(clear) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_lifetime(pe, 100, 100);
    rt_particle_emitter_burst(pe, 50);
    ASSERT(rt_particle_emitter_count(pe) == 50);

    rt_particle_emitter_clear(pe);
    ASSERT(rt_particle_emitter_count(pe) == 0);
    rt_particle_emitter_destroy(pe);
}

TEST(continuous_emission) {
    rt_particle_emitter pe = rt_particle_emitter_new(100);
    rt_particle_emitter_set_lifetime(pe, 100, 100);
    rt_particle_emitter_set_rate(pe, 10.0);  // 10 per frame
    rt_particle_emitter_start(pe);

    rt_particle_emitter_update(pe);
    ASSERT(rt_particle_emitter_count(pe) >= 9);  // ~10 particles
    ASSERT(rt_particle_emitter_count(pe) <= 11);
    rt_particle_emitter_destroy(pe);
}

TEST(max_particles) {
    rt_particle_emitter pe = rt_particle_emitter_new(20);  // Max 20
    rt_particle_emitter_set_lifetime(pe, 100, 100);

    rt_particle_emitter_burst(pe, 50);  // Try to emit 50
    ASSERT(rt_particle_emitter_count(pe) == 20);  // Capped at 20
    rt_particle_emitter_destroy(pe);
}

int main() {
    printf("RTParticleTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(set_position);
    RUN_TEST(burst);
    RUN_TEST(start_stop);
    RUN_TEST(update_lifetime);
    RUN_TEST(clear);
    RUN_TEST(continuous_emission);
    RUN_TEST(max_particles);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
