// tui/tests/test_config.cpp
// @brief Verify configuration loader parses theme, keymap, and editor settings.
// @invariant Parsed values match expected sample config.
// @ownership Test owns configuration data only.

#include "tui/config/config.hpp"

#include <cassert>

using viper::tui::config::Config;
using viper::tui::config::loadFromFile;
using viper::tui::render::RGBA;
using viper::tui::term::KeyEvent;

int main()
{
    Config cfg;
    bool ok = loadFromFile(CONFIG_INI, cfg);
    assert(ok);

    // Theme color
    assert((cfg.theme.accent.bg == RGBA{200, 200, 200, 255}));

    // Editor settings
    assert(cfg.editor.tab_width == 2);
    assert(cfg.editor.soft_wrap);

    // Keymap binding
    bool found_save = false;
    for (const auto &b : cfg.keymap_global)
    {
        if (b.command == "save")
        {
            found_save = true;
            assert((b.chord.mods & KeyEvent::Ctrl) != 0);
            assert(b.chord.codepoint == 'S' || b.chord.codepoint == 's');
        }
    }
    assert(found_save);

    // Invalid tab width should keep default while parsing other values.
    Config cfg_bad;
    bool ok_bad = loadFromFile(CONFIG_BAD_TAB_INI, cfg_bad);
    assert(ok_bad);
    assert(cfg_bad.editor.tab_width == 4);
    assert(cfg_bad.editor.soft_wrap);
    return 0;
}
