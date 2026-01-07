//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_text_view.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/views/text_view.hpp"

#include <cassert>

using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::text::TextBuffer;
using viper::tui::ui::Event;
using viper::tui::views::TextView;

int main()
{
    Theme theme;
    TextBuffer buf;
    buf.load("alpha\nbeta\ngamma\ndelta");

    TextView view(buf, theme, false);
    view.layout({0, 0, 10, 2});

    // Move to second line and end
    Event ev{};
    ev.key.code = KeyEvent::Code::Down;
    view.onEvent(ev);
    ev.key.code = KeyEvent::Code::End;
    view.onEvent(ev);
    assert(view.cursorRow() == 1);
    assert(view.cursorCol() == 4);

    // Page down to last line (height=2)
    ev.key.code = KeyEvent::Code::PageDown;
    view.onEvent(ev);
    assert(view.cursorRow() == 3);

    // Home and select first char with shift+right
    ev.key.code = KeyEvent::Code::Home;
    ev.key.mods = 0;
    view.onEvent(ev);
    ev.key.code = KeyEvent::Code::Right;
    ev.key.mods = KeyEvent::Mods::Shift;
    view.onEvent(ev);
    assert(view.cursorCol() == 1);

    // Paint and verify selection
    ScreenBuffer sb;
    sb.resize(2, 10);
    sb.clear(theme.style(Role::Normal));
    view.paint(sb);
    const auto &selStyle = theme.style(Role::Selection);
    assert(sb.at(1, 0).ch == U'd');
    assert(sb.at(1, 0).style == selStyle);
    assert(sb.at(0, 0).ch == U'g');
    assert(sb.at(0, 0).style == theme.style(Role::Normal));

    // Page up back to second line
    ev.key.code = KeyEvent::Code::PageUp;
    ev.key.mods = 0;
    view.onEvent(ev);
    assert(view.cursorRow() == 1);

    return 0;
}
