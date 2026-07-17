//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestRuntimeSearchOptionApis.cpp
// Purpose: Tests Option-returning search APIs added by the runtime public API
//          overhaul.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_compiled_pattern.h"
#include "rt_lazyseq.h"
#include "rt_list.h"
#include "rt_mixgroup.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_regex.h"
#include "rt_scene.h"
#include "rt_scene_editor.h"
#include "rt_seq.h"
#include "rt_seq_functional.h"
#include "rt_string.h"
#include "rt_unionfind.h"
#include "rt_xml.h"
}

/// @brief Release a runtime object test handle when its reference count reaches zero.
/// @param obj Runtime object handle, or NULL.
static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Predicate used by Seq/LazySeq tests to find the value 20.
/// @param value Boxed i64 value for Seq tests, or int64_t pointer for LazySeq tests.
/// @return 1 when @p value represents 20, 0 otherwise.
static int8_t is_twenty(void *value) {
    if (!value)
        return 0;
    if (rt_box_type(value) == RT_BOX_I64)
        return rt_unbox_i64(value) == 20 ? 1 : 0;
    return *static_cast<int64_t *>(value) == 20 ? 1 : 0;
}

/// @brief Assert that an Option contains the expected i64 value.
/// @param option Opaque Zanna.Option object.
/// @param expected Expected integer payload.
static void expect_some_i64(void *option, int64_t expected) {
    ASSERT_TRUE(option != nullptr);
    EXPECT_EQ(rt_option_is_some(option), 1);
    EXPECT_EQ(rt_option_unwrap_i64(option), expected);
}

/// @brief Assert that an Option is None.
/// @param option Opaque Zanna.Option object.
static void expect_none(void *option) {
    ASSERT_TRUE(option != nullptr);
    EXPECT_EQ(rt_option_is_none(option), 1);
}

TEST(RuntimeSearchOptionApis, CollectionFindOptionsReturnIndexes) {
    void *bytes = rt_bytes_from_str(rt_const_cstr("hello"));
    void *byte_hit = rt_bytes_find_option(bytes, 'e');
    expect_some_i64(byte_hit, 1);
    release_obj(byte_hit);
    void *byte_miss = rt_bytes_find_option(bytes, 'z');
    expect_none(byte_miss);
    release_obj(byte_miss);
    release_obj(bytes);

    void *list = rt_list_new();
    void *a = rt_box_i64(10);
    void *b = rt_box_i64(20);
    rt_list_push(list, a);
    rt_list_push(list, b);
    void *list_hit = rt_list_find_option(list, b);
    expect_some_i64(list_hit, 1);
    release_obj(list_hit);
    void *list_needle = rt_box_i64(30);
    void *list_miss = rt_list_find_option(list, list_needle);
    expect_none(list_miss);
    release_obj(list_miss);
    release_obj(list_needle);
    release_obj(list);

    void *seq = rt_seq_new();
    rt_seq_push(seq, rt_box_i64(10));
    rt_seq_push(seq, rt_box_i64(20));
    void *seq_needle = rt_box_i64(20);
    void *seq_hit = rt_seq_find_option(seq, seq_needle);
    expect_some_i64(seq_hit, 1);
    release_obj(seq_hit);
    release_obj(seq_needle);
    void *where_hit = rt_seq_find_where_option_wrapper(seq, reinterpret_cast<void *>(is_twenty));
    ASSERT_TRUE(where_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(where_hit), 1);
    EXPECT_EQ(rt_unbox_i64(rt_option_unwrap(where_hit)), 20);
    release_obj(where_hit);
    release_obj(seq);

    void *uf = rt_unionfind_new(4);
    ASSERT_TRUE(uf != nullptr);
    EXPECT_EQ(rt_unionfind_union(uf, 0, 1), 1);
    void *root_hit = rt_unionfind_find_root_option(uf, 1);
    ASSERT_TRUE(root_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(root_hit), 1);
    EXPECT_GE(rt_option_unwrap_i64(root_hit), 0);
    release_obj(root_hit);
    void *root_miss = rt_unionfind_find_root_option(uf, 99);
    expect_none(root_miss);
    release_obj(root_miss);
    release_obj(uf);
}

TEST(RuntimeSearchOptionApis, RegexFindOptionsPreserveEmptyMatches) {
    void *empty_match = rt_pattern_find_option(rt_const_cstr("abc"), rt_const_cstr("[0-9]*"));
    ASSERT_TRUE(empty_match != nullptr);
    EXPECT_EQ(rt_option_is_some(empty_match), 1);
    EXPECT_EQ(rt_str_len(rt_option_unwrap_str(empty_match)), 0);
    release_obj(empty_match);

    void *pos = rt_pattern_find_pos_option(rt_const_cstr("abc123"), rt_const_cstr("[0-9]+"));
    expect_some_i64(pos, 3);
    release_obj(pos);

    void *from_miss =
        rt_pattern_find_from_option(rt_const_cstr("abc123"), rt_const_cstr("[0-9]+"), 6);
    expect_none(from_miss);
    release_obj(from_miss);

    void *compiled = rt_compiled_pattern_new(rt_const_cstr("[a-z]*"));
    void *compiled_empty = rt_compiled_pattern_find_option(compiled, rt_const_cstr("123"));
    ASSERT_TRUE(compiled_empty != nullptr);
    EXPECT_EQ(rt_option_is_some(compiled_empty), 1);
    EXPECT_EQ(rt_str_len(rt_option_unwrap_str(compiled_empty)), 0);
    release_obj(compiled_empty);
    release_obj(compiled);
}

TEST(RuntimeSearchOptionApis, XmlAndSceneFindOptionsReturnObjects) {
    void *doc = rt_xml_parse(rt_const_cstr("<root><item id=\"1\"/></root>"));
    ASSERT_TRUE(doc != nullptr);
    void *xml_hit = rt_xml_find_option(doc, rt_const_cstr("item"));
    ASSERT_TRUE(xml_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(xml_hit), 1);
    release_obj(xml_hit);
    void *xml_miss = rt_xml_find_option(doc, rt_const_cstr("missing"));
    expect_none(xml_miss);
    release_obj(xml_miss);
    release_obj(doc);

    void *scene = rt_scene_new();
    void *node = rt_scene_node_new();
    rt_scene_node_set_name(node, rt_const_cstr("player"));
    rt_scene_add(scene, node);
    void *scene_hit = rt_scene_find_option(scene, rt_const_cstr("player"));
    ASSERT_TRUE(scene_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(scene_hit), 1);
    release_obj(scene_hit);
    void *node_hit = rt_scene_node_find_option(node, rt_const_cstr("player"));
    ASSERT_TRUE(node_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(node_hit), 1);
    release_obj(node_hit);
    void *scene_miss = rt_scene_find_option(scene, rt_const_cstr("enemy"));
    expect_none(scene_miss);
    release_obj(scene_miss);
    release_obj(scene);
    release_obj(node);
}

TEST(RuntimeSearchOptionApis, SceneDocumentAudioAndLazySeqOptions) {
    void *doc = rt_game_scene_new(4, 4, 16, 16);
    ASSERT_TRUE(doc != nullptr);
    EXPECT_EQ(rt_game_scene_add_object(doc, rt_const_cstr("Player"), rt_const_cstr("hero"), 1, 2),
              0);
    void *object_hit = rt_game_scene_find_object_option(doc, rt_const_cstr("hero"));
    expect_some_i64(object_hit, 0);
    release_obj(object_hit);
    void *object_miss = rt_game_scene_find_object_option(doc, rt_const_cstr("missing"));
    expect_none(object_miss);
    release_obj(object_miss);
    release_obj(doc);

    int64_t group_id = rt_audio_register_group(rt_const_cstr("search-option-test"));
    ASSERT_TRUE(group_id >= 0);
    void *group_hit = rt_audio_find_group_option(rt_const_cstr("search-option-test"));
    expect_some_i64(group_hit, group_id);
    release_obj(group_hit);
    void *group_miss = rt_audio_find_group_option(rt_const_cstr("definitely-missing-group"));
    expect_none(group_miss);
    release_obj(group_miss);

    void *lazy = rt_lazyseq_w_range(10, 30, 10);
    void *lazy_hit = rt_lazyseq_w_find_option(lazy, reinterpret_cast<void *>(is_twenty));
    ASSERT_TRUE(lazy_hit != nullptr);
    EXPECT_EQ(rt_option_is_some(lazy_hit), 1);
    EXPECT_EQ(*static_cast<int64_t *>(rt_option_unwrap(lazy_hit)), 20);
    release_obj(lazy_hit);
    rt_lazyseq_destroy(static_cast<rt_lazyseq>(lazy));
}

int main() {
    return zanna_test::run_all_tests();
}
