// tui/tests/test_keymap_palette.cpp
// @brief Validate keymap scopes and CommandPalette filtering/execution.
// @invariant Widget-specific bindings override global and palette runs callbacks.
// @ownership Theme and widgets owned by test.

#include "tui/input/keymap.hpp"
#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/term_io.hpp"
#include "tui/widgets/command_palette.hpp"
#include "tui/widgets/label.hpp"

#include <cassert>

using viper::tui::term::StringTermIO;
using viper::tui::input::KeyChord;
using viper::tui::input::Keymap;
using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::ui::Event;
using viper::tui::widgets::CommandPalette;
using viper::tui::widgets::Label;

int main()
{
    Theme theme;
    Keymap km;

    bool global_fired = false;
    bool widget_fired = false;
    bool save_fired = false;

    km.registerCommand("global", "Global", [&] { global_fired = true; });
    km.registerCommand("widget", "Widget", [&] { widget_fired = true; });
    km.registerCommand("save", "Save", [&] { save_fired = true; });

    KeyChord gkc{KeyEvent::Code::F1, 0, 0};
    km.bindGlobal(gkc, "global");

    Label lbl("L", theme);
    KeyChord wkc{KeyEvent::Code::F2, 0, 0};
    km.bindWidget(&lbl, wkc, "widget");

    KeyEvent ev{};
    ev.code = KeyEvent::Code::F1;
    assert(km.handle(nullptr, ev));
    assert(global_fired);

    ev.code = KeyEvent::Code::F2;
    assert(!km.handle(nullptr, ev));
    assert(!widget_fired);
    assert(km.handle(&lbl, ev));
    assert(widget_fired);

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
    assert(tio.buffer().find("Save") != std::string::npos);

    e.key.code = KeyEvent::Code::Enter;
    e.key.codepoint = 0;
    cp.onEvent(e);
    assert(save_fired);

    return 0;
}
