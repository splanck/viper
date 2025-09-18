// tui/tests/test_search.cpp
// @brief Tests for TextBuffer search and SearchBar widget.
// @invariant findAll/findNext locate matches and SearchBar cycles highlights.
// @ownership Test owns buffer, theme, view, and search bar.

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/key_event.hpp"
#include "tui/text/search.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/views/text_view.hpp"
#include "tui/widgets/search_bar.hpp"

#include <cassert>

using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::text::findAll;
using viper::tui::text::findNext;
using viper::tui::text::TextBuffer;
using viper::tui::ui::Event;
using viper::tui::views::TextView;
using viper::tui::widgets::SearchBar;

int main()
{
    TextBuffer buf;
    buf.load("alpha beta alpha gamma alpha");

    auto hits = findAll(buf, "alpha", false);
    assert(hits.size() == 3);
    auto next = findNext(buf, "alpha", hits[0].start + 1, false);
    assert(next && next->start == hits[1].start);

    auto rhits = findAll(buf, "alpha", true);
    assert(rhits.size() == 3);

    Theme theme;
    TextView view(buf, theme, false);
    view.layout({0, 0, 40, 1});
    SearchBar bar(buf, view, theme);
    bar.layout({0, 1, 40, 1});

    Event ev{};
    const char *pat = "alpha";
    for (int i = 0; pat[i]; ++i)
    {
        ev.key.code = KeyEvent::Code::Unknown;
        ev.key.codepoint = static_cast<uint32_t>(pat[i]);
        bar.onEvent(ev);
    }
    assert(bar.matchCount() == 3);
    assert(view.cursorCol() == 0);

    ev.key.code = KeyEvent::Code::Enter;
    bar.onEvent(ev);
    assert(view.cursorCol() == 11);

    ScreenBuffer sb;
    sb.resize(1, 40);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);
    assert(sb.at(0, 11).style == theme.style(Role::Accent));

    return 0;
}
