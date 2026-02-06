//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTStateMachineTests.cpp - Unit tests for rt_statemachine
//===----------------------------------------------------------------------===//

#include "rt_statemachine.h"
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
    rt_statemachine sm = rt_statemachine_new();
    ASSERT(sm != NULL);
    ASSERT(rt_statemachine_current(sm) == -1);
    ASSERT(rt_statemachine_state_count(sm) == 0);
    rt_statemachine_destroy(sm);
}

TEST(add_states)
{
    rt_statemachine sm = rt_statemachine_new();
    ASSERT(rt_statemachine_add_state(sm, 0) == 1);
    ASSERT(rt_statemachine_add_state(sm, 1) == 1);
    ASSERT(rt_statemachine_add_state(sm, 2) == 1);
    ASSERT(rt_statemachine_state_count(sm) == 3);
    // Duplicate should fail
    ASSERT(rt_statemachine_add_state(sm, 1) == 0);
    ASSERT(rt_statemachine_state_count(sm) == 3);
    rt_statemachine_destroy(sm);
}

TEST(set_initial)
{
    rt_statemachine sm = rt_statemachine_new();
    rt_statemachine_add_state(sm, 0);
    rt_statemachine_add_state(sm, 1);
    ASSERT(rt_statemachine_set_initial(sm, 0) == 1);
    ASSERT(rt_statemachine_current(sm) == 0);
    ASSERT(rt_statemachine_just_entered(sm) == 1);
    rt_statemachine_destroy(sm);
}

TEST(transition)
{
    rt_statemachine sm = rt_statemachine_new();
    rt_statemachine_add_state(sm, 0);
    rt_statemachine_add_state(sm, 1);
    rt_statemachine_add_state(sm, 2);
    rt_statemachine_set_initial(sm, 0);
    rt_statemachine_clear_flags(sm);

    ASSERT(rt_statemachine_transition(sm, 1) == 1);
    ASSERT(rt_statemachine_current(sm) == 1);
    ASSERT(rt_statemachine_previous(sm) == 0);
    ASSERT(rt_statemachine_just_entered(sm) == 1);
    ASSERT(rt_statemachine_just_exited(sm) == 1);

    // Invalid transition
    ASSERT(rt_statemachine_transition(sm, 99) == 0);
    ASSERT(rt_statemachine_current(sm) == 1);
    rt_statemachine_destroy(sm);
}

TEST(frames_in_state)
{
    rt_statemachine sm = rt_statemachine_new();
    rt_statemachine_add_state(sm, 0);
    rt_statemachine_set_initial(sm, 0);

    ASSERT(rt_statemachine_frames_in_state(sm) == 0);
    rt_statemachine_update(sm);
    ASSERT(rt_statemachine_frames_in_state(sm) == 1);
    rt_statemachine_update(sm);
    ASSERT(rt_statemachine_frames_in_state(sm) == 2);
    rt_statemachine_destroy(sm);
}

TEST(is_state)
{
    rt_statemachine sm = rt_statemachine_new();
    rt_statemachine_add_state(sm, 0);
    rt_statemachine_add_state(sm, 1);
    rt_statemachine_set_initial(sm, 0);

    ASSERT(rt_statemachine_is_state(sm, 0) == 1);
    ASSERT(rt_statemachine_is_state(sm, 1) == 0);
    ASSERT(rt_statemachine_has_state(sm, 0) == 1);
    ASSERT(rt_statemachine_has_state(sm, 99) == 0);
    rt_statemachine_destroy(sm);
}

int main()
{
    printf("RTStateMachineTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(add_states);
    RUN_TEST(set_initial);
    RUN_TEST(transition);
    RUN_TEST(frames_in_state);
    RUN_TEST(is_state);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
