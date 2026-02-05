//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_keymap_palette.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/input/keymap.hpp"
#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/term_io.hpp"
#include "tui/widgets/command_palette.hpp"
#include "tui/widgets/label.hpp"

#include "tests/TestHarness.hpp"

#include <algorithm>

using viper::tui::input::KeyChord;
using viper::tui::input::Keymap;
using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Event;
using viper::tui::widgets::CommandPalette;
using viper::tui::widgets::Label;

TEST(TUI, KeymapPalette)
{
    Theme theme;
    Keymap km;

    bool global_fired = false;
    bool widget_fired = false;
    int save_legacy_calls = 0;
    bool save_fired = false;

    km.registerCommand("global", "Global", [&] { global_fired = true; });
    km.registerCommand("widget", "Widget", [&] { widget_fired = true; });
    km.registerCommand("save", "Save", [&] { ++save_legacy_calls; });
    km.registerCommand("save", "Save Document", [&] { save_fired = true; });

    ASSERT_EQ(km.commands().size(), 3);
    const auto save_count = std::count_if(km.commands().begin(),
                                          km.commands().end(),
                                          [](const auto &cmd) { return cmd.id == "save"; });
    ASSERT_EQ(save_count, 1);
    const auto *save_cmd = km.find("save");
    ASSERT_TRUE(save_cmd != nullptr);
    ASSERT_EQ(save_cmd->name, "Save Document");
    ASSERT_TRUE(km.execute("save"));
    ASSERT_TRUE(save_fired);
    ASSERT_EQ(save_legacy_calls, 0);
    save_fired = false;

    KeyChord gkc{KeyEvent::Code::F1, 0, 0};
    km.bindGlobal(gkc, "global");

    Label lbl("L", theme);
    KeyChord wkc{KeyEvent::Code::F2, 0, 0};
    km.bindWidget(&lbl, wkc, "widget");

    KeyEvent ev{};
    ev.code = KeyEvent::Code::F1;
    ASSERT_TRUE(km.handle(nullptr, ev));
    ASSERT_TRUE(global_fired);

    ev.code = KeyEvent::Code::F2;
    ASSERT_FALSE(km.handle(nullptr, ev));
    ASSERT_FALSE(widget_fired);
    ASSERT_TRUE(km.handle(&lbl, ev));
    ASSERT_TRUE(widget_fired);

    CommandPalette cp(km, theme);
    cp.layout({0, 0, 10, 3});

    Event e{};
    e.key.code = KeyEvent::Code::Unknown;
    e.key.codepoint = U's';
    cp.onEvent(e);
    e.key.codepoint = U'a';
    cp.onEvent(e);

    ScreenBuffer sb;
    sb.resize(3, 10);
    sb.clear(theme.style(Role::Normal));
    cp.paint(sb);

    StringTermIO tio;
    Renderer r(tio, true);
    r.draw(sb);
    ASSERT_TRUE(tio.buffer().find("Save") != std::string::npos);

    e.key.code = KeyEvent::Code::Enter;
    e.key.codepoint = 0;
    cp.onEvent(e);
    ASSERT_TRUE(save_fired);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
