//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestTypewriter.cpp
// Purpose: Tests for the Typewriter text-reveal runtime.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "rt_string.h"
#include "rt_typewriter.h"
}

TEST(Typewriter, NegativeDeltaIsNoOp) {
    rt_typewriter tw = rt_typewriter_new();
    rt_typewriter_say(tw, "abc", 10);

    EXPECT_EQ(rt_typewriter_char_count(tw), 0);
    EXPECT_FALSE(rt_typewriter_update(tw, -100));
    EXPECT_EQ(rt_typewriter_char_count(tw), 0);
    EXPECT_EQ(rt_typewriter_progress(tw), 0);

    rt_typewriter_update(tw, 10);
    EXPECT_EQ(rt_typewriter_char_count(tw), 1);

    rt_typewriter_destroy(tw);
}

TEST(Typewriter, LargeDeltaCompletesAndClampsProgress) {
    rt_typewriter tw = rt_typewriter_new();
    rt_typewriter_say(tw, "abc", 10);

    EXPECT_TRUE(rt_typewriter_update(tw, INT64_MAX));
    EXPECT_TRUE(rt_typewriter_is_complete(tw));
    EXPECT_EQ(rt_typewriter_char_count(tw), 3);
    EXPECT_EQ(rt_typewriter_progress(tw), 100);

    rt_string visible = rt_typewriter_get_visible_text(tw);
    EXPECT_EQ(std::strcmp(rt_string_cstr(visible), "abc"), 0);

    rt_typewriter_destroy(tw);
}

TEST(Typewriter, EmptyTextCompletesImmediately) {
    rt_typewriter tw = rt_typewriter_new();
    rt_typewriter_say(tw, "", 10);

    EXPECT_FALSE(rt_typewriter_is_active(tw));
    EXPECT_TRUE(rt_typewriter_is_complete(tw));
    EXPECT_EQ(rt_typewriter_char_count(tw), 0);
    EXPECT_EQ(rt_typewriter_total_chars(tw), 0);
    EXPECT_EQ(rt_typewriter_progress(tw), 100);
    EXPECT_FALSE(rt_typewriter_update(tw, 10));

    rt_string visible = rt_typewriter_get_visible_text(tw);
    EXPECT_EQ(std::strcmp(rt_string_cstr(visible), ""), 0);

    rt_typewriter_destroy(tw);
}

TEST(Typewriter, RevealsUtf8CodepointsAtomically) {
    const char text[] = {'A', static_cast<char>(0xC3), static_cast<char>(0xA9), 'B', '\0'};
    const char expected_second[] = {'A', static_cast<char>(0xC3), static_cast<char>(0xA9), '\0'};

    rt_typewriter tw = rt_typewriter_new();
    rt_typewriter_say(tw, text, 10);

    EXPECT_EQ(rt_typewriter_total_chars(tw), 3);
    EXPECT_FALSE(rt_typewriter_update(tw, 10));
    EXPECT_EQ(rt_typewriter_char_count(tw), 1);
    rt_string visible = rt_typewriter_get_visible_text(tw);
    EXPECT_EQ(std::strcmp(rt_string_cstr(visible), "A"), 0);

    EXPECT_FALSE(rt_typewriter_update(tw, 10));
    EXPECT_EQ(rt_typewriter_char_count(tw), 2);
    visible = rt_typewriter_get_visible_text(tw);
    EXPECT_EQ(std::strcmp(rt_string_cstr(visible), expected_second), 0);

    EXPECT_TRUE(rt_typewriter_update(tw, 10));
    EXPECT_EQ(rt_typewriter_char_count(tw), 3);
    EXPECT_EQ(rt_typewriter_progress(tw), 100);

    rt_typewriter_destroy(tw);
}

int main() {
    return viper_test::run_all_tests();
}
