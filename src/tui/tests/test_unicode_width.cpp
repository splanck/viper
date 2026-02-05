//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_unicode_width.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/util/unicode.hpp"

#include "tests/TestHarness.hpp"

using viper::tui::util::char_width;
using viper::tui::util::decode_utf8;

TEST(TUI, UnicodeWidth)
{
    auto s = decode_utf8("A");
    ASSERT_EQ(s.size(), 1);
    ASSERT_EQ(char_width(s[0]), 1);

    s = decode_utf8("\xE4\xBD\xA0"); // 你
    ASSERT_EQ(s.size(), 1);
    ASSERT_EQ(char_width(s[0]), 2);

    s = decode_utf8("e\xCC\x81"); // e + combining acute
    ASSERT_EQ(s.size(), 2);
    ASSERT_EQ(char_width(s[0]), 1);
    ASSERT_EQ(char_width(s[1]), 0);

    s = decode_utf8("\xC0\xAF"); // overlong '/'
    ASSERT_EQ(s.size(), 2);
    ASSERT_TRUE(s[0] == 0xFFFD && s[1] == 0xFFFD);

    s = decode_utf8("\xED\xA0\x80"); // surrogate U+D800
    ASSERT_EQ(s.size(), 3);
    for (auto ch : s)
    {
        ASSERT_EQ(ch, 0xFFFD);
    }

    s = decode_utf8("\xF4\x90\x80\x80"); // > U+10FFFF
    ASSERT_EQ(s.size(), 4);
    for (auto ch : s)
    {
        ASSERT_EQ(ch, 0xFFFD);
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
