//===----------------------------------------------------------------------===//
// Tests for Viper.Game.Typewriter edge cases.
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
    EXPECT_FALSE(rt_typewriter_update(tw, 10));

    rt_string visible = rt_typewriter_get_visible_text(tw);
    EXPECT_EQ(std::strcmp(rt_string_cstr(visible), ""), 0);

    rt_typewriter_destroy(tw);
}

int main() {
    return viper_test::run_all_tests();
}
