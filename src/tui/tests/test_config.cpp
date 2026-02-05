//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_config.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/config/config.hpp"

#include "tests/TestHarness.hpp"

using viper::tui::config::Config;
using viper::tui::config::loadFromFile;
using viper::tui::render::RGBA;
using viper::tui::term::KeyEvent;

TEST(TUI, Config)
{
    Config cfg;
    bool ok = loadFromFile(CONFIG_INI, cfg);
    ASSERT_TRUE(ok);

    // Theme color
    ASSERT_TRUE((cfg.theme.accent.bg == RGBA{200, 200, 200, 255}));

    // Editor settings
    ASSERT_EQ(cfg.editor.tab_width, 2);
    ASSERT_TRUE(cfg.editor.soft_wrap);

    // Keymap binding
    bool found_save = false;
    for (const auto &b : cfg.keymap_global)
    {
        if (b.command == "save")
        {
            found_save = true;
            ASSERT_TRUE((b.chord.mods & KeyEvent::Ctrl) != 0);
            ASSERT_TRUE(b.chord.codepoint == 'S' || b.chord.codepoint == 's');
        }
    }
    ASSERT_TRUE(found_save);

    // Invalid tab width should keep default while parsing other values.
    Config cfg_bad;
    bool ok_bad = loadFromFile(CONFIG_BAD_TAB_INI, cfg_bad);
    ASSERT_TRUE(ok_bad);
    ASSERT_EQ(cfg_bad.editor.tab_width, 4);
    ASSERT_TRUE(cfg_bad.editor.soft_wrap);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
