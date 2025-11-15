// tui/tests/test_widgets_basic.cpp
// @brief Verify basic Label and Button behavior.
// @invariant Painting writes expected glyphs and button handles keys.
// @ownership Widgets and theme owned within test.

#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/term_io.hpp"
#include "tui/widgets/button.hpp"
#include "tui/widgets/label.hpp"

#include <cassert>
#include <string>

using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Event;
using viper::tui::widgets::Button;
using viper::tui::widgets::Label;

int main()
{
    Theme theme;

    // Label paint
    Label lbl("Hello", theme);
    lbl.layout({0, 0, 5, 1});

    ScreenBuffer sb;
    sb.resize(1, 5);
    sb.clear(theme.style(Role::Normal));
    lbl.paint(sb);

    StringTermIO tio;
    Renderer r(tio, true);
    r.draw(sb);
    assert(tio.buffer().find("Hello") != std::string::npos);

    // Button paint and onClick
    bool clicked = false;
    Button btn("Go", [&] { clicked = true; }, theme);
    btn.layout({0, 0, 4, 3});
    sb.resize(3, 4);
    sb.clear(theme.style(Role::Normal));
    btn.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find("Go") != std::string::npos);
    assert(tio.buffer().find("+--+") != std::string::npos);

    Event ev{};
    ev.key.code = KeyEvent::Code::Enter;
    assert(btn.onEvent(ev));
    assert(clicked);

    clicked = false;
    ev.key.code = KeyEvent::Code::Unknown;
    ev.key.codepoint = U' ';
    assert(btn.onEvent(ev));
    assert(clicked);

    return 0;
}
