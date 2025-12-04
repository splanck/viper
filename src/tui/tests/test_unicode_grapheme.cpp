//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_unicode_grapheme.cpp
// Purpose: Test Unicode grapheme handling including combining marks, full-width
//          characters, and cursor navigation over complex text.
// Key invariants: Cursor movement respects grapheme boundaries; display width
//                 accounts for combining marks (0-width) and full-width CJK (2).
// Ownership/Lifetime: Tests are self-contained with no external dependencies.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/util/unicode.hpp"
#include "tui/views/text_view.hpp"

#include <cassert>
#include <string>

using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::text::TextBuffer;
using viper::tui::ui::Event;
using viper::tui::util::char_width;
using viper::tui::util::decode_utf8;
using viper::tui::views::TextView;

// UTF-8 byte sequences for test characters:
// 中 = E4 B8 AD (U+4E2D)
// 文 = E6 96 87 (U+6587)
// combining acute = CC 81 (U+0301)
// combining grave = CC 80 (U+0300)

// Helper constants to avoid hex escape issues with following characters.
static const char kCjkZhong[] = "\xE4\xB8\xAD";   // 中
static const char kCjkWen[] = "\xE6\x96\x87";     // 文
static const char kCombiningAcute[] = "\xCC\x81"; // U+0301
static const char kCombiningGrave[] = "\xCC\x80"; // U+0300

//===----------------------------------------------------------------------===//
// char_width tests for combining marks and full-width characters
//===----------------------------------------------------------------------===//

// Test combining marks have zero width.
void test_combining_marks_width()
{
    // U+0301 COMBINING ACUTE ACCENT
    assert(char_width(0x0301) == 0);
    // U+0300 COMBINING GRAVE ACCENT
    assert(char_width(0x0300) == 0);
    // U+0302 COMBINING CIRCUMFLEX ACCENT
    assert(char_width(0x0302) == 0);
    // U+0308 COMBINING DIAERESIS (umlaut)
    assert(char_width(0x0308) == 0);
    // U+036F COMBINING LATIN SMALL LETTER X (end of range)
    assert(char_width(0x036F) == 0);
}

// Test full-width CJK characters have width 2.
void test_cjk_full_width()
{
    // Common CJK ideographs
    assert(char_width(0x4E2D) == 2); // 中
    assert(char_width(0x6587) == 2); // 文
    assert(char_width(0x5B57) == 2); // 字
    // Korean Hangul syllables
    assert(char_width(0xAC00) == 2); // 가
    assert(char_width(0xD7A3) == 2); // end of Hangul syllables
    // Japanese Hiragana (in wide range)
    assert(char_width(0x3042) == 2); // あ
    // Japanese Katakana
    assert(char_width(0x30A2) == 2); // ア
    // Fullwidth Latin
    assert(char_width(0xFF21) == 2); // Ａ (fullwidth A)
}

// Test ASCII and other characters have width 1.
void test_normal_width()
{
    assert(char_width('A') == 1);
    assert(char_width('z') == 1);
    assert(char_width(' ') == 1);
    assert(char_width('0') == 1);
    // Emoji (not in wide range, treated as width 1 by our simplified impl)
    assert(char_width(0x1F600) == 1); // grinning face
}

//===----------------------------------------------------------------------===//
// decode_utf8 tests with combining sequences
//===----------------------------------------------------------------------===//

// Test decoding base + multiple combining marks.
void test_decode_multiple_combining()
{
    // "e" + combining acute + combining grave = 3 code points
    std::string input = std::string("e") + kCombiningAcute + kCombiningGrave;
    auto s = decode_utf8(input);
    assert(s.size() == 3);
    assert(s[0] == 'e');
    assert(s[1] == 0x0301); // acute
    assert(s[2] == 0x0300); // grave
    // Total display width: 1 + 0 + 0 = 1
    int w = 0;
    for (auto cp : s)
        w += char_width(cp);
    assert(w == 1);
}

// Test decoding mixed ASCII and CJK.
void test_decode_mixed_ascii_cjk()
{
    // "a中b文c"
    std::string input = std::string("a") + kCjkZhong + "b" + kCjkWen + "c";
    auto s = decode_utf8(input);
    assert(s.size() == 5);
    assert(s[0] == 'a');
    assert(s[1] == 0x4E2D); // 中
    assert(s[2] == 'b');
    assert(s[3] == 0x6587); // 文
    assert(s[4] == 'c');
    // Width: 1 + 2 + 1 + 2 + 1 = 7
    int w = 0;
    for (auto cp : s)
        w += char_width(cp);
    assert(w == 7);
}

// Test CJK string width calculation.
void test_cjk_string_width()
{
    // "你好" (2 characters, each width 2 = total 4)
    auto s = decode_utf8("\xE4\xBD\xA0\xE5\xA5\xBD");
    assert(s.size() == 2);
    int w = 0;
    for (auto cp : s)
        w += char_width(cp);
    assert(w == 4);
}

//===----------------------------------------------------------------------===//
// TextBuffer tests with Unicode content
//===----------------------------------------------------------------------===//

// Test TextBuffer stores and retrieves Unicode correctly.
void test_buffer_unicode_storage()
{
    TextBuffer buf;
    // Line with combining mark: "café" where é = e + combining acute
    std::string cafe = std::string("caf") + "e" + kCombiningAcute;
    buf.load(cafe);
    assert(buf.lineCount() == 1);
    std::string line = buf.getLine(0);
    assert(line == cafe);
    assert(line.size() == 6); // c a f e combining_acute = 6 bytes
}

// Test TextBuffer insert with CJK.
void test_buffer_insert_cjk()
{
    TextBuffer buf;
    buf.load("ab");
    // Insert 中 between a and b
    buf.insert(1, kCjkZhong);
    std::string expected = std::string("a") + kCjkZhong + "b";
    assert(buf.getLine(0) == expected);
}

// Test TextBuffer erase within CJK character.
void test_buffer_erase_cjk()
{
    TextBuffer buf;
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);
    // Erase the CJK character (3 bytes at position 1)
    buf.erase(1, 3);
    assert(buf.getLine(0) == "ab");
}

// Test TextBuffer erase combining sequence as unit.
void test_buffer_erase_combining()
{
    TextBuffer buf;
    std::string aeAcuteB = std::string("a") + "e" + kCombiningAcute + "b";
    buf.load(aeAcuteB);
    // Erase "é" = e + combining = 3 bytes at position 1
    buf.erase(1, 3);
    assert(buf.getLine(0) == "ab");
}

//===----------------------------------------------------------------------===//
// TextView cursor navigation tests with Unicode
//===----------------------------------------------------------------------===//

// Test cursor navigation over CJK (full-width) characters.
void test_view_cursor_cjk()
{
    Theme theme;
    TextBuffer buf;
    // "a中b" = a(1) + 中(2) + b(1) = 4 display columns
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    // Start at column 0
    assert(view.cursorRow() == 0);
    assert(view.cursorCol() == 0);

    // Move right: should go from col 0 to col 1 (after 'a')
    Event ev{};
    ev.key.code = KeyEvent::Code::Right;
    view.onEvent(ev);
    assert(view.cursorCol() == 1);

    // Move right: should skip over 中 (width 2) to col 3
    view.onEvent(ev);
    assert(view.cursorCol() == 3);

    // Move right: should go to col 4 (after 'b', end of line)
    view.onEvent(ev);
    assert(view.cursorCol() == 4);

    // Move left: should go back to col 3
    ev.key.code = KeyEvent::Code::Left;
    view.onEvent(ev);
    assert(view.cursorCol() == 3);

    // Move left: should skip back over 中 to col 1
    view.onEvent(ev);
    assert(view.cursorCol() == 1);
}

// Test cursor navigation over combining marks.
void test_view_cursor_combining()
{
    Theme theme;
    TextBuffer buf;
    // "aéb" where é = e + combining acute
    // Display: a(1) + é(1+0=1) + b(1) = 3 columns
    std::string aeAcuteB = std::string("a") + "e" + kCombiningAcute + "b";
    buf.load(aeAcuteB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    assert(view.cursorCol() == 0);

    // Move right to after 'a'
    Event ev{};
    ev.key.code = KeyEvent::Code::Right;
    view.onEvent(ev);
    assert(view.cursorCol() == 1);

    // Move right: should move over 'e' + combining (treated as 1 column)
    view.onEvent(ev);
    // After 'é', column should be 2
    assert(view.cursorCol() == 2);

    // Move right to after 'b'
    view.onEvent(ev);
    assert(view.cursorCol() == 3);
}

// Test moveCursorToOffset with CJK content.
void test_view_move_to_offset_cjk()
{
    Theme theme;
    TextBuffer buf;
    // "中文" = two CJK chars, 6 bytes, 4 display columns
    std::string zhongWen = std::string(kCjkZhong) + kCjkWen;
    buf.load(zhongWen);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    // Move to start
    view.moveCursorToOffset(0);
    assert(view.cursorCol() == 0);

    // Move to byte 3 (start of second char)
    view.moveCursorToOffset(3);
    assert(view.cursorCol() == 2); // First char takes 2 columns

    // Move to end
    view.moveCursorToOffset(6);
    assert(view.cursorCol() == 4);
}

// Test End key with mixed-width line.
void test_view_end_key_mixed()
{
    Theme theme;
    TextBuffer buf;
    // "a中b" = 4 display columns
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    Event ev{};
    ev.key.code = KeyEvent::Code::End;
    view.onEvent(ev);
    assert(view.cursorCol() == 4);
}

// Test Home key resets to column 0.
void test_view_home_key()
{
    Theme theme;
    TextBuffer buf;
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    // Move to end first
    Event ev{};
    ev.key.code = KeyEvent::Code::End;
    view.onEvent(ev);
    assert(view.cursorCol() == 4);

    // Home should go back to 0
    ev.key.code = KeyEvent::Code::Home;
    view.onEvent(ev);
    assert(view.cursorCol() == 0);
}

//===----------------------------------------------------------------------===//
// TextView rendering tests with Unicode
//===----------------------------------------------------------------------===//

// Test rendering CJK characters to screen buffer.
void test_render_cjk()
{
    Theme theme;
    TextBuffer buf;
    buf.load(kCjkZhong); // 中

    TextView view(buf, theme, false);
    view.layout({0, 0, 10, 1});

    ScreenBuffer sb;
    sb.resize(1, 10);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);

    // First cell should have the CJK character
    assert(sb.at(0, 0).ch == 0x4E2D);
    assert(sb.at(0, 0).width == 2);
}

// Test rendering combining mark sequence.
void test_render_combining()
{
    Theme theme;
    TextBuffer buf;
    // "é" = e + combining acute
    std::string eAcute = std::string("e") + kCombiningAcute;
    buf.load(eAcute);

    TextView view(buf, theme, false);
    view.layout({0, 0, 10, 1});

    ScreenBuffer sb;
    sb.resize(1, 10);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);

    // First cell should have 'e'
    assert(sb.at(0, 0).ch == 'e');
    // Second cell should have the combining mark (width 0)
    assert(sb.at(0, 1).ch == 0x0301);
    // The combining mark cell should have width 0
    assert(sb.at(0, 1).width == 0);
}

// Test rendering mixed ASCII and CJK.
void test_render_mixed()
{
    Theme theme;
    TextBuffer buf;
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 10, 1});

    ScreenBuffer sb;
    sb.resize(1, 10);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);

    // Column 0: 'a'
    assert(sb.at(0, 0).ch == 'a');
    assert(sb.at(0, 0).width == 1);
    // Column 1: '中' (width 2)
    assert(sb.at(0, 1).ch == 0x4E2D);
    assert(sb.at(0, 1).width == 2);
    // Column 3: 'b' (column 2 is continuation of wide char)
    assert(sb.at(0, 3).ch == 'b');
    assert(sb.at(0, 3).width == 1);
}

//===----------------------------------------------------------------------===//
// Multi-line tests with Unicode
//===----------------------------------------------------------------------===//

// Test vertical navigation with varying line widths.
void test_view_vertical_nav_mixed()
{
    Theme theme;
    TextBuffer buf;
    // Line 0: "中文" (4 columns)
    // Line 1: "ab" (2 columns)
    std::string zhongWenNewlineAb = std::string(kCjkZhong) + kCjkWen + "\nab";
    buf.load(zhongWenNewlineAb);

    TextView view(buf, theme, false);
    view.layout({0, 0, 20, 5});

    // Move to end of first line (column 4)
    Event ev{};
    ev.key.code = KeyEvent::Code::End;
    view.onEvent(ev);
    assert(view.cursorRow() == 0);
    assert(view.cursorCol() == 4);

    // Move down: target column 4, but line 1 only has 2 columns
    ev.key.code = KeyEvent::Code::Down;
    view.onEvent(ev);
    assert(view.cursorRow() == 1);
    assert(view.cursorCol() == 2); // Clamped to end of shorter line

    // Move up: should try to restore target column 4
    ev.key.code = KeyEvent::Code::Up;
    view.onEvent(ev);
    assert(view.cursorRow() == 0);
    assert(view.cursorCol() == 4);
}

// Test selection with CJK characters.
void test_view_selection_cjk()
{
    Theme theme;
    TextBuffer buf;
    std::string aZhongB = std::string("a") + kCjkZhong + "b";
    buf.load(aZhongB);

    TextView view(buf, theme, false);
    view.layout({0, 0, 10, 1});

    // Select first two visual characters (a + 中)
    Event ev{};
    ev.key.code = KeyEvent::Code::Right;
    ev.key.mods = KeyEvent::Mods::Shift;

    // Shift+Right once: select 'a'
    view.onEvent(ev);
    assert(view.cursorCol() == 1);

    // Shift+Right again: select '中'
    view.onEvent(ev);
    assert(view.cursorCol() == 3);

    // Render and check selection styling
    ScreenBuffer sb;
    sb.resize(1, 10);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);

    const auto &selStyle = theme.style(Role::Selection);
    // 'a' should be selected
    assert(sb.at(0, 0).style == selStyle);
    // '中' should be selected
    assert(sb.at(0, 1).style == selStyle);
    // 'b' should NOT be selected
    assert(sb.at(0, 3).style != selStyle);
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main()
{
    // char_width tests
    test_combining_marks_width();
    test_cjk_full_width();
    test_normal_width();

    // decode_utf8 tests
    test_decode_multiple_combining();
    test_decode_mixed_ascii_cjk();
    test_cjk_string_width();

    // TextBuffer tests
    test_buffer_unicode_storage();
    test_buffer_insert_cjk();
    test_buffer_erase_cjk();
    test_buffer_erase_combining();

    // TextView cursor tests
    test_view_cursor_cjk();
    test_view_cursor_combining();
    test_view_move_to_offset_cjk();
    test_view_end_key_mixed();
    test_view_home_key();

    // TextView render tests
    test_render_cjk();
    test_render_combining();
    test_render_mixed();

    // Multi-line tests
    test_view_vertical_nav_mixed();
    test_view_selection_cjk();

    return 0;
}
