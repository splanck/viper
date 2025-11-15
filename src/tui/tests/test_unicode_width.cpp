// tui/tests/test_unicode_width.cpp
// @brief Tests Unicode width calculation and UTF-8 decoding.
// @invariant Combining marks and CJK ranges have expected widths.
// @ownership decode_utf8 returns owned strings used locally.

#include "tui/util/unicode.hpp"

#include <cassert>

using viper::tui::util::char_width;
using viper::tui::util::decode_utf8;

int main()
{
    auto s = decode_utf8("A");
    assert(s.size() == 1);
    assert(char_width(s[0]) == 1);

    s = decode_utf8("\xE4\xBD\xA0"); // 你
    assert(s.size() == 1);
    assert(char_width(s[0]) == 2);

    s = decode_utf8("e\xCC\x81"); // e + combining acute
    assert(s.size() == 2);
    assert(char_width(s[0]) == 1);
    assert(char_width(s[1]) == 0);

    s = decode_utf8("\xC0\xAF"); // overlong '/'
    assert(s.size() == 2);
    assert(s[0] == 0xFFFD && s[1] == 0xFFFD);

    s = decode_utf8("\xED\xA0\x80"); // surrogate U+D800
    assert(s.size() == 3);
    for (auto ch : s)
    {
        assert(ch == 0xFFFD);
    }

    s = decode_utf8("\xF4\x90\x80\x80"); // > U+10FFFF
    assert(s.size() == 4);
    for (auto ch : s)
    {
        assert(ch == 0xFFFD);
    }

    return 0;
}
