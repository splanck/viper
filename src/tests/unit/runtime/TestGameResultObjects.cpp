//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestGameResultObjects.cpp
// Purpose: Tests for composable game result objects introduced by the runtime
//          API overhaul.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_animation_events.h"
#include "rt_animstate.h"
#include "rt_animtimeline.h"
#include "rt_list.h"
#include "rt_object.h"
#include "rt_pathfinder.h"
#include "rt_quadtree.h"
#include "rt_string.h"
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

TEST(GameResultObjects, PathResultCapturesFoundPath) {
    void *pf = rt_pathfinder_new(4, 4);
    ASSERT_TRUE(pf != nullptr);

    void *result = rt_pathfinder_find_path_result(pf, 0, 0, 3, 0);
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_path_result_found(result), 1);
    EXPECT_EQ(rt_path_result_step_count(result), 3);
    EXPECT_EQ(rt_path_result_length(result), 3);
    EXPECT_TRUE(rt_path_result_steps(result) > 0);
    EXPECT_TRUE(rt_path_result_cost(result) >= 300);

    void *path = rt_path_result_path(result);
    ASSERT_TRUE(path != nullptr);
    EXPECT_EQ(rt_list_len(path), 4);

    release_obj(path);
    release_obj(result);
    release_obj(pf);
}

TEST(GameResultObjects, PathResultCapturesMissWithoutLastStateRead) {
    void *pf = rt_pathfinder_new(3, 3);
    ASSERT_TRUE(pf != nullptr);
    rt_pathfinder_set_walkable(pf, 1, 0, 0);
    rt_pathfinder_set_walkable(pf, 1, 1, 0);
    rt_pathfinder_set_walkable(pf, 1, 2, 0);

    void *result = rt_pathfinder_find_path_result(pf, 0, 0, 2, 0);
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_path_result_found(result), 0);
    EXPECT_EQ(rt_path_result_step_count(result), -1);
    EXPECT_EQ(rt_path_result_length(result), -1);

    void *path = rt_path_result_path(result);
    ASSERT_TRUE(path != nullptr);
    EXPECT_EQ(rt_list_len(path), 0);

    release_obj(path);
    release_obj(result);
    release_obj(pf);
}

TEST(GameResultObjects, QuadtreeQueryResultIsSnapshot) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 100, 100);
    ASSERT_TRUE(tree != nullptr);
    EXPECT_EQ(rt_quadtree_insert(tree, 10, 10, 10, 8, 8), 1);
    EXPECT_EQ(rt_quadtree_insert(tree, 20, 40, 40, 8, 8), 1);

    void *result = rt_quadtree_query_rect_result(tree, 0, 0, 20, 20);
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_game_query_result_count(result), 1);
    EXPECT_EQ(rt_game_query_result_get_id(result, 0), 10);
    EXPECT_EQ(rt_game_query_result_contains(result, 10), 1);
    EXPECT_EQ(rt_game_query_result_truncated(result), 0);

    EXPECT_EQ(rt_quadtree_query_rect(tree, 80, 80, 5, 5), 0);
    EXPECT_EQ(rt_game_query_result_count(result), 1);
    EXPECT_EQ(rt_game_query_result_get_id(result, 0), 10);

    release_obj(result);
    release_obj(tree);
}

TEST(GameResultObjects, QuadtreePairResultIsSnapshot) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 100, 100);
    ASSERT_TRUE(tree != nullptr);
    EXPECT_EQ(rt_quadtree_insert(tree, 1, 20, 20, 20, 20), 1);
    EXPECT_EQ(rt_quadtree_insert(tree, 2, 24, 24, 20, 20), 1);

    void *pairs = rt_quadtree_query_pairs(tree);
    ASSERT_TRUE(pairs != nullptr);
    EXPECT_TRUE(rt_quadtree_pair_result_count(pairs) >= 1);
    EXPECT_EQ(rt_quadtree_pair_result_first(pairs, 0), 1);
    EXPECT_EQ(rt_quadtree_pair_result_second(pairs, 0), 2);
    EXPECT_EQ(rt_quadtree_pair_result_truncated(pairs), 0);

    rt_quadtree_clear(tree);
    EXPECT_TRUE(rt_quadtree_pair_result_count(pairs) >= 1);

    release_obj(pairs);
    release_obj(tree);
}

TEST(GameResultObjects, AnimStatePollEventsReturnsSnapshot) {
    void *sm = rt_animstate_new();
    ASSERT_TRUE(sm != nullptr);
    rt_animstate_add_state(sm, 0, 0, 4, 1, 0);
    EXPECT_EQ(rt_animstate_add_event(sm, 0, 1, 101), 1);
    EXPECT_EQ(rt_animstate_add_event(sm, 0, 2, 102), 1);
    EXPECT_EQ(rt_animstate_set_initial(sm, 0), 1);

    rt_animstate_update(sm);
    void *first = rt_animstate_poll_events(sm);
    ASSERT_TRUE(first != nullptr);
    EXPECT_EQ(rt_animation_event_batch_count(first), 1);
    EXPECT_EQ(rt_animation_event_batch_get_id(first, 0), 101);
    EXPECT_EQ(rt_animation_event_batch_contains(first, 101), 1);

    rt_animstate_update(sm);
    void *second = rt_animstate_poll_events(sm);
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(rt_animation_event_batch_get_id(second, 0), 102);
    EXPECT_EQ(rt_animation_event_batch_get_id(first, 0), 101);

    release_obj(second);
    release_obj(first);
    release_obj(sm);
}

TEST(GameResultObjects, AnimTimelinePollEventsReturnsSnapshot) {
    void *tl = rt_animtimeline_new(10);
    ASSERT_TRUE(tl != nullptr);
    EXPECT_EQ(rt_animtimeline_add_marker(tl, 2, 42), 0);
    rt_animtimeline_play(tl);
    rt_animtimeline_advance(tl, 3);

    void *batch = rt_animtimeline_poll_events(tl);
    ASSERT_TRUE(batch != nullptr);
    EXPECT_EQ(rt_animation_event_batch_count(batch), 1);
    EXPECT_EQ(rt_animation_event_batch_get_id(batch, 0), 42);
    EXPECT_EQ(rt_animation_event_batch_contains(batch, 99), 0);

    release_obj(batch);
    release_obj(tl);
}

int main() {
    return viper_test::run_all_tests();
}
