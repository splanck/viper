//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTObjPoolTests.cpp - Unit tests for rt_objpool
//===----------------------------------------------------------------------===//

#include "rt_objpool.h"
#include <cassert>
#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name)                                                                             \
    do                                                                                             \
    {                                                                                              \
        printf("  %s...", #name);                                                                  \
        test_##name();                                                                             \
        printf(" OK\n");                                                                           \
        tests_passed++;                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf(" FAILED at line %d: %s\n", __LINE__, #cond);                                   \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

TEST(create_destroy)
{
    rt_objpool pool = rt_objpool_new(100);
    ASSERT(pool != NULL);
    ASSERT(rt_objpool_capacity(pool) == 100);
    ASSERT(rt_objpool_active_count(pool) == 0);
    ASSERT(rt_objpool_free_count(pool) == 100);
    ASSERT(rt_objpool_is_empty(pool) == 1);
    ASSERT(rt_objpool_is_full(pool) == 0);
    rt_objpool_destroy(pool);
}

TEST(acquire_release)
{
    rt_objpool pool = rt_objpool_new(10);

    int64_t slot1 = rt_objpool_acquire(pool);
    ASSERT(slot1 >= 0);
    ASSERT(rt_objpool_is_active(pool, slot1) == 1);
    ASSERT(rt_objpool_active_count(pool) == 1);

    int64_t slot2 = rt_objpool_acquire(pool);
    ASSERT(slot2 >= 0);
    ASSERT(slot2 != slot1);
    ASSERT(rt_objpool_active_count(pool) == 2);

    ASSERT(rt_objpool_release(pool, slot1) == 1);
    ASSERT(rt_objpool_is_active(pool, slot1) == 0);
    ASSERT(rt_objpool_active_count(pool) == 1);

    rt_objpool_destroy(pool);
}

TEST(pool_full)
{
    rt_objpool pool = rt_objpool_new(3);

    rt_objpool_acquire(pool);
    rt_objpool_acquire(pool);
    rt_objpool_acquire(pool);

    ASSERT(rt_objpool_is_full(pool) == 1);
    ASSERT(rt_objpool_acquire(pool) == -1); // Full

    rt_objpool_destroy(pool);
}

TEST(slot_reuse)
{
    rt_objpool pool = rt_objpool_new(5);

    int64_t slot1 = rt_objpool_acquire(pool);
    rt_objpool_release(pool, slot1);
    int64_t slot2 = rt_objpool_acquire(pool);

    ASSERT(slot2 == slot1); // Reused slot
    rt_objpool_destroy(pool);
}

TEST(clear)
{
    rt_objpool pool = rt_objpool_new(10);

    rt_objpool_acquire(pool);
    rt_objpool_acquire(pool);
    rt_objpool_acquire(pool);
    ASSERT(rt_objpool_active_count(pool) == 3);

    rt_objpool_clear(pool);
    ASSERT(rt_objpool_active_count(pool) == 0);
    ASSERT(rt_objpool_is_empty(pool) == 1);

    rt_objpool_destroy(pool);
}

TEST(iterate_active)
{
    rt_objpool pool = rt_objpool_new(10);

    int64_t slots[5];
    for (int i = 0; i < 5; i++)
    {
        slots[i] = rt_objpool_acquire(pool);
    }
    // Release middle one
    rt_objpool_release(pool, slots[2]);

    // Iterate and count
    int count = 0;
    int64_t slot = rt_objpool_first_active(pool);
    while (slot >= 0)
    {
        count++;
        ASSERT(slot != slots[2]); // Should not see released slot
        slot = rt_objpool_next_active(pool, slot);
    }
    ASSERT(count == 4);

    rt_objpool_destroy(pool);
}

TEST(user_data)
{
    rt_objpool pool = rt_objpool_new(10);

    int64_t slot = rt_objpool_acquire(pool);
    ASSERT(rt_objpool_set_data(pool, slot, 12345) == 1);
    ASSERT(rt_objpool_get_data(pool, slot) == 12345);

    // Invalid slot
    ASSERT(rt_objpool_set_data(pool, 99, 100) == 0);
    ASSERT(rt_objpool_get_data(pool, 99) == 0);

    rt_objpool_destroy(pool);
}

TEST(invalid_operations)
{
    rt_objpool pool = rt_objpool_new(5);

    // Release invalid slot
    ASSERT(rt_objpool_release(pool, -1) == 0);
    ASSERT(rt_objpool_release(pool, 100) == 0);

    // Check invalid slot
    ASSERT(rt_objpool_is_active(pool, -1) == 0);
    ASSERT(rt_objpool_is_active(pool, 100) == 0);

    rt_objpool_destroy(pool);
}

TEST(capacity_limits)
{
    // Test minimum capacity
    rt_objpool small = rt_objpool_new(0);
    ASSERT(rt_objpool_capacity(small) >= 1);
    rt_objpool_destroy(small);

    // Test max capacity clamping
    rt_objpool large = rt_objpool_new(100000);
    ASSERT(rt_objpool_capacity(large) <= RT_OBJPOOL_MAX);
    rt_objpool_destroy(large);
}

int main()
{
    printf("RTObjPoolTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(acquire_release);
    RUN_TEST(pool_full);
    RUN_TEST(slot_reuse);
    RUN_TEST(clear);
    RUN_TEST(iterate_active);
    RUN_TEST(user_data);
    RUN_TEST(invalid_operations);
    RUN_TEST(capacity_limits);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
