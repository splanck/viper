//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_widgets_basic.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/term_io.hpp"
#include "tui/widgets/button.hpp"
#include "tui/widgets/label.hpp"

#include "tests/TestHarness.hpp"
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

TEST(TUI, WidgetsBasic)
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
    ASSERT_TRUE(tio.buffer().find("Hello") != std::string::npos);

    // Button paint and onClick
    bool clicked = false;
    Button btn("Go", [&] { clicked = true; }, theme);
    btn.layout({0, 0, 4, 3});
    sb.resize(3, 4);
    sb.clear(theme.style(Role::Normal));
    btn.paint(sb);
    tio.clear();
    r.draw(sb);
    ASSERT_TRUE(tio.buffer().find("Go") != std::string::npos);
    ASSERT_TRUE(tio.buffer().find("+--+") != std::string::npos);

    Event ev{};
    ev.key.code = KeyEvent::Code::Enter;
    ASSERT_TRUE(btn.onEvent(ev));
    ASSERT_TRUE(clicked);

    clicked = false;
    ev.key.code = KeyEvent::Code::Unknown;
    ev.key.codepoint = U' ';
    ASSERT_TRUE(btn.onEvent(ev));
    ASSERT_TRUE(clicked);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
