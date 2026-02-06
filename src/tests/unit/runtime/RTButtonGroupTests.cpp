//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTButtonGroupTests.cpp - Unit tests for rt_buttongroup
//===----------------------------------------------------------------------===//

#include "rt_buttongroup.h"
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
    rt_buttongroup bg = rt_buttongroup_new();
    ASSERT(bg != NULL);
    ASSERT(rt_buttongroup_count(bg) == 0);
    ASSERT(rt_buttongroup_selected(bg) == -1);
    ASSERT(rt_buttongroup_has_selection(bg) == 0);
    rt_buttongroup_destroy(bg);
}

TEST(add_buttons)
{
    rt_buttongroup bg = rt_buttongroup_new();
    ASSERT(rt_buttongroup_add(bg, 1) == 1);
    ASSERT(rt_buttongroup_add(bg, 2) == 1);
    ASSERT(rt_buttongroup_add(bg, 3) == 1);
    ASSERT(rt_buttongroup_count(bg) == 3);
    // Duplicate
    ASSERT(rt_buttongroup_add(bg, 2) == 0);
    ASSERT(rt_buttongroup_count(bg) == 3);
    // Has
    ASSERT(rt_buttongroup_has(bg, 1) == 1);
    ASSERT(rt_buttongroup_has(bg, 99) == 0);
    rt_buttongroup_destroy(bg);
}

TEST(select)
{
    rt_buttongroup bg = rt_buttongroup_new();
    rt_buttongroup_add(bg, 1);
    rt_buttongroup_add(bg, 2);
    rt_buttongroup_add(bg, 3);

    ASSERT(rt_buttongroup_select(bg, 2) == 1);
    ASSERT(rt_buttongroup_selected(bg) == 2);
    ASSERT(rt_buttongroup_has_selection(bg) == 1);
    ASSERT(rt_buttongroup_is_selected(bg, 2) == 1);
    ASSERT(rt_buttongroup_is_selected(bg, 1) == 0);
    ASSERT(rt_buttongroup_selection_changed(bg) == 1);

    rt_buttongroup_clear_changed_flag(bg);
    ASSERT(rt_buttongroup_selection_changed(bg) == 0);

    // Select non-existent
    ASSERT(rt_buttongroup_select(bg, 99) == 0);
    ASSERT(rt_buttongroup_selected(bg) == 2);
    rt_buttongroup_destroy(bg);
}

TEST(clear_selection)
{
    rt_buttongroup bg = rt_buttongroup_new();
    rt_buttongroup_add(bg, 1);
    rt_buttongroup_add(bg, 2);
    rt_buttongroup_select(bg, 1);

    rt_buttongroup_clear_selection(bg);
    ASSERT(rt_buttongroup_selected(bg) == -1);
    ASSERT(rt_buttongroup_has_selection(bg) == 0);
    rt_buttongroup_destroy(bg);
}

TEST(select_next_prev)
{
    rt_buttongroup bg = rt_buttongroup_new();
    rt_buttongroup_add(bg, 10);
    rt_buttongroup_add(bg, 20);
    rt_buttongroup_add(bg, 30);
    rt_buttongroup_select(bg, 10);

    int64_t next = rt_buttongroup_select_next(bg);
    ASSERT(next == 20);
    ASSERT(rt_buttongroup_selected(bg) == 20);

    next = rt_buttongroup_select_next(bg);
    ASSERT(next == 30);

    // Wrap around
    next = rt_buttongroup_select_next(bg);
    ASSERT(next == 10);

    // Prev
    int64_t prev = rt_buttongroup_select_prev(bg);
    ASSERT(prev == 30);
    rt_buttongroup_destroy(bg);
}

TEST(remove)
{
    rt_buttongroup bg = rt_buttongroup_new();
    rt_buttongroup_add(bg, 1);
    rt_buttongroup_add(bg, 2);
    rt_buttongroup_add(bg, 3);
    rt_buttongroup_select(bg, 2);

    ASSERT(rt_buttongroup_remove(bg, 2) == 1);
    ASSERT(rt_buttongroup_count(bg) == 2);
    ASSERT(rt_buttongroup_selected(bg) == -1); // Selection cleared
    ASSERT(rt_buttongroup_has(bg, 2) == 0);
    rt_buttongroup_destroy(bg);
}

TEST(get_at)
{
    rt_buttongroup bg = rt_buttongroup_new();
    rt_buttongroup_add(bg, 100);
    rt_buttongroup_add(bg, 200);
    rt_buttongroup_add(bg, 300);

    ASSERT(rt_buttongroup_get_at(bg, 0) == 100);
    ASSERT(rt_buttongroup_get_at(bg, 1) == 200);
    ASSERT(rt_buttongroup_get_at(bg, 2) == 300);
    ASSERT(rt_buttongroup_get_at(bg, 99) == -1);
    rt_buttongroup_destroy(bg);
}

int main()
{
    printf("RTButtonGroupTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(add_buttons);
    RUN_TEST(select);
    RUN_TEST(clear_selection);
    RUN_TEST(select_next_prev);
    RUN_TEST(remove);
    RUN_TEST(get_at);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
