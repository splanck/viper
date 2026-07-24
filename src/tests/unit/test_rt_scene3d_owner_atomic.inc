//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_scene3d_owner_atomic.inc
// Purpose: Scene3D subtree-owner transaction fault-injection regression coverage.
//
// Key invariants:
//   - Failed owner-stack growth changes no parent, owner, child list, or scene count.
//   - Wide-subtree coverage forces the second geometric traversal-stack growth.
//
// Ownership/Lifetime:
//   - Included by test_rt_scene3d.cpp after its trap helper and Scene3D fixtures.
//   - Runtime objects remain owned by the test runtime heap for process lifetime.
//
// Links: test_rt_scene3d.cpp, rt_scene3d_internal.h, ADR 0173
//
//===----------------------------------------------------------------------===//

/// @brief Verify subtree owner propagation stages capacity before changing any node or scene.
/// @details A fault-injected cross-scene move of a wide subtree must leave both scene counts,
/// parent links, and every descendant owner unchanged. The 80 leaves force traversal storage to
/// grow past its initial capacity, exercising the exact preflight failure boundary.
static void test_owner_propagation_allocation_failure_is_atomic() {
    rt_scene3d *scene_a = static_cast<rt_scene3d *>(rt_scene3d_new());
    rt_scene3d *scene_b = static_cast<rt_scene3d *>(rt_scene3d_new());
    auto *parent_a = static_cast<rt_scene_node3d *>(rt_scene_node3d_new());
    auto *parent_b = static_cast<rt_scene_node3d *>(rt_scene_node3d_new());
    auto *subtree = static_cast<rt_scene_node3d *>(rt_scene_node3d_new());
    std::vector<rt_scene_node3d *> leaves;

    rt_scene3d_add(scene_a, parent_a);
    rt_scene3d_add(scene_b, parent_b);
    rt_scene_node3d_add_child(parent_a, subtree);
    for (int i = 0; i < 80; ++i) {
        auto *leaf = static_cast<rt_scene_node3d *>(rt_scene_node3d_new());
        leaves.push_back(leaf);
        rt_scene_node3d_add_child(subtree, leaf);
    }

    const int64_t count_a = rt_scene3d_get_node_count(scene_a);
    const int64_t count_b = rt_scene3d_get_node_count(scene_b);
    rt_scene3d_test_set_owner_growth_failure(1);
    const bool trapped =
        expect_trap_contains([&] { (void)rt_scene_node3d_try_add_child(parent_b, subtree); },
                             "owner traversal stack allocation failed");
    rt_scene3d_test_set_owner_growth_failure(-1);

    EXPECT_TRUE(trapped, "Owner propagation reports an injected second-growth failure");
    EXPECT_TRUE(subtree->parent == parent_a,
                "Owner preflight failure preserves the original parent");
    EXPECT_TRUE(subtree->owner_scene == scene_a,
                "Owner preflight failure preserves the subtree root owner");
    EXPECT_TRUE(rt_scene_node3d_child_count(parent_a) == 1,
                "Owner preflight failure preserves source children");
    EXPECT_TRUE(rt_scene_node3d_child_count(parent_b) == 0,
                "Owner preflight failure preserves destination children");
    EXPECT_TRUE(rt_scene3d_get_node_count(scene_a) == count_a,
                "Owner preflight failure preserves source scene count");
    EXPECT_TRUE(rt_scene3d_get_node_count(scene_b) == count_b,
                "Owner preflight failure preserves destination scene count");
    for (rt_scene_node3d *leaf : leaves) {
        EXPECT_TRUE(leaf->owner_scene == scene_a,
                    "Owner preflight failure preserves every descendant owner");
        EXPECT_TRUE(leaf->parent == subtree,
                    "Owner preflight failure preserves every descendant parent");
    }
}
