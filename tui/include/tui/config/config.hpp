// tui/include/tui/config/config.hpp
// @brief Load INI-like configuration for theme, keymap, and editor settings.
// @invariant Parsed values remain valid after load.
// @ownership Config owns parsed data only.
#pragma once

#include <string>
#include <vector>

#include "tui/input/keymap.hpp"
#include "tui/render/screen.hpp"

namespace viper::tui::config
{

/// @brief Theme colors for roles.
struct Theme
{
    render::Style normal{};
    render::Style accent{};
    render::Style disabled{};
    render::Style selection{};
};

/// @brief Editor-specific settings.
struct Editor
{
    unsigned tab_width{4};
    bool soft_wrap{false};
};

/// @brief Key binding entry.
struct Binding
{
    input::KeyChord chord{};
    input::CommandId command{};
};

/// @brief Aggregated configuration.
struct Config
{
    Theme theme{};
    std::vector<Binding> keymap_global{};
    Editor editor{};
};

/// @brief Load configuration from file path.
/// @param path INI-like file to parse.
/// @param out Filled with parsed configuration.
/// @return True if file parsed successfully.
bool loadFromFile(const std::string &path, Config &out);

} // namespace viper::tui::config
