//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_search.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/text/search.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/views/text_view.hpp"
#include "tui/widgets/search_bar.hpp"

#include "tests/TestHarness.hpp"

using zanna::tui::render::ScreenBuffer;
using zanna::tui::style::Role;
using zanna::tui::style::Theme;
using zanna::tui::term::KeyEvent;
using zanna::tui::text::findAll;
using zanna::tui::text::findNext;
using zanna::tui::text::TextBuffer;
using zanna::tui::ui::Event;
using zanna::tui::views::TextView;
using zanna::tui::widgets::SearchBar;

TEST(TUI, Search) {
    TextBuffer buf;
    buf.load("alpha beta alpha gamma alpha");

    auto hits = findAll(buf, "alpha", false);
    ASSERT_EQ(hits.size(), 3);
    auto next = findNext(buf, "alpha", hits[0].start + 1, false);
    ASSERT_TRUE(next && next->start == hits[1].start);

    auto rhits = findAll(buf, "alpha", true);
    ASSERT_EQ(rhits.size(), 3);

    Theme theme;
    TextView view(buf, theme, false);
    view.layout({0, 0, 40, 1});
    SearchBar bar(buf, view, theme);
    bar.layout({0, 1, 40, 1});

    Event ev{};
    const char *pat = "alpha";
    for (int i = 0; pat[i]; ++i) {
        ev.key.code = KeyEvent::Code::Unknown;
        ev.key.codepoint = static_cast<uint32_t>(pat[i]);
        bar.onEvent(ev);
    }
    ASSERT_EQ(bar.matchCount(), 3);
    ASSERT_EQ(view.cursorCol(), 0);

    ev.key.code = KeyEvent::Code::Enter;
    bar.onEvent(ev);
    ASSERT_EQ(view.cursorCol(), 11);

    ScreenBuffer sb;
    sb.resize(1, 40);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);
    ASSERT_EQ(sb.at(0, 11).style, theme.style(Role::Accent));
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
