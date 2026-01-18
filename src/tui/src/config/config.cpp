//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/config/config.cpp
// Purpose: Implement the INI-style configuration loader shared by ViperTUI
//          applications.
// Key invariants: Only the [theme], [keymap.global], and [editor] sections are
//                 recognized today, and unknown keys are ignored so that
//                 defaults remain intact.
// Ownership/Lifetime: The loader materializes data into the supplied Config
//                     instance and otherwise borrows file descriptors and
//                     string views.
// Links: docs/tools.md#configuration
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parses INI-like configuration files into @ref viper::tui::config::Config.
/// @details The implementation favours robustness: leading/trailing whitespace
///          is trimmed, unrecognized sections are skipped, and malformed entries
///          leave the destination structure untouched.  Helpers live in an
///          anonymous namespace to keep the translation unit self-contained.

#include "tui/config/config.hpp"

#include "tui/util/color.hpp"
#include "tui/util/string.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

namespace viper::tui::config
{

namespace
{
/// @brief Remove leading and trailing ASCII whitespace from a string view.
/// @details The helper walks the bounds of the provided view without copying
///          intermediate substrings and then returns an owning @c std::string
///          containing the trimmed characters.
/// @param sv Source view to trim.
/// @return A new @c std::string with surrounding whitespace stripped.
std::string trim(std::string_view sv)
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
    {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

/// @brief Convert a symbolic key name into a @ref term::KeyEvent::Code.
/// @details Handles named control keys (``enter``, ``esc``), arrow keys, page
///          navigation, delete/insert, and function keys (``f1``–``f12``).  Any
///          unrecognized name maps to @c Code::Unknown.
/// @param name Case-insensitive key name token.
/// @return The corresponding @ref term::KeyEvent::Code enumerator.
term::KeyEvent::Code parse_code(const std::string &name)
{
    using Code = term::KeyEvent::Code;
    if (name == "enter")
        return Code::Enter;
    if (name == "esc")
        return Code::Esc;
    if (name == "tab")
        return Code::Tab;
    if (name == "backspace")
        return Code::Backspace;
    if (name == "up")
        return Code::Up;
    if (name == "down")
        return Code::Down;
    if (name == "left")
        return Code::Left;
    if (name == "right")
        return Code::Right;
    if (name == "home")
        return Code::Home;
    if (name == "end")
        return Code::End;
    if (name == "pageup")
        return Code::PageUp;
    if (name == "pagedown")
        return Code::PageDown;
    if (name == "insert")
        return Code::Insert;
    if (name == "delete")
        return Code::Delete;
    if (name.size() > 1 && name[0] == 'f')
    {
        int num = std::stoi(name.substr(1));
        switch (num)
        {
            case 1:
                return Code::F1;
            case 2:
                return Code::F2;
            case 3:
                return Code::F3;
            case 4:
                return Code::F4;
            case 5:
                return Code::F5;
            case 6:
                return Code::F6;
            case 7:
                return Code::F7;
            case 8:
                return Code::F8;
            case 9:
                return Code::F9;
            case 10:
                return Code::F10;
            case 11:
                return Code::F11;
            case 12:
                return Code::F12;
            default:
                break;
        }
    }
    return Code::Unknown;
}

/// @brief Parse a key chord specification such as ``Ctrl+Shift+K``.
/// @details The routine recognises modifier tokens (Ctrl, Alt, Shift) and
///          delegates to @ref parse_code for named keys.  Single-character tokens
///          are interpreted as Unicode code points.  Missing components produce
///          default-initialised modifiers.
/// @param str User-supplied chord string.
/// @return Normalised key chord ready for configuration binding.
input::KeyChord parse_chord(const std::string &str)
{
    input::KeyChord kc{};
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, '+'))
    {
        token = trim(token);
        std::string lower = util::toLower(token);
        if (lower == "ctrl")
        {
            kc.mods |= term::KeyEvent::Ctrl;
            continue;
        }
        if (lower == "alt")
        {
            kc.mods |= term::KeyEvent::Alt;
            continue;
        }
        if (lower == "shift")
        {
            kc.mods |= term::KeyEvent::Shift;
            continue;
        }
        if (token.size() == 1)
        {
            kc.codepoint = static_cast<uint32_t>(token[0]);
        }
        else
        {
            kc.code = parse_code(lower);
        }
    }
    return kc;
}

/// @brief Interpret a boolean configuration value.
/// @details Accepts ``1``, ``true``, or ``yes`` (case-insensitive) as true and
///          treats all other strings as false.
/// @param s Raw value token to normalise.
/// @return @c true when @p s represents a truthy value.
bool parse_bool(const std::string &s)
{
    std::string lower = util::toLower(s);
    return lower == "1" || lower == "true" || lower == "yes";
}

} // namespace

/// @brief Load configuration data from an INI-like file.
/// @details The loader iterates line-by-line, ignoring comments and blank
///          lines, dispatching recognised sections to specialised handlers.  On
///          failure to open the file the function returns @c false without
///          mutating @p out.  Successfully parsed keys update the corresponding
///          substructures in place.
/// @param path Filesystem path to read.
/// @param out Destination configuration populated with parsed values.
/// @return @c true when the file was opened and processed.
bool loadFromFile(const std::string &path, Config &out)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }
    std::string line;
    std::string section;
    while (std::getline(in, line))
    {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
        {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            section = trimmed.substr(1, trimmed.size() - 2);
            util::toLowerInPlace(section);
            continue;
        }
        auto eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        std::string lower_key = util::toLower(key);

        if (section == "theme")
        {
            render::RGBA col;
            if (!util::parseHexColor(value, col))
            {
                continue;
            }
            if (lower_key == "normal_fg")
                out.theme.normal.fg = col;
            else if (lower_key == "normal_bg")
                out.theme.normal.bg = col;
            else if (lower_key == "accent_fg")
                out.theme.accent.fg = col;
            else if (lower_key == "accent_bg")
                out.theme.accent.bg = col;
            else if (lower_key == "disabled_fg")
                out.theme.disabled.fg = col;
            else if (lower_key == "disabled_bg")
                out.theme.disabled.bg = col;
            else if (lower_key == "selection_fg")
                out.theme.selection.fg = col;
            else if (lower_key == "selection_bg")
                out.theme.selection.bg = col;
        }
        else if (section == "keymap.global")
        {
            Binding b{};
            b.chord = parse_chord(key);
            b.command = value;
            out.keymap_global.push_back(b);
        }
        else if (section == "editor")
        {
            if (lower_key == "tab_width")
            {
                try
                {
                    std::size_t consumed = 0;
                    const unsigned long parsed = std::stoul(value, &consumed);
                    if (consumed != value.size() || parsed == 0)
                    {
                        continue;
                    }
                    const unsigned max_width = std::numeric_limits<unsigned>::max();
                    const unsigned clamped =
                        parsed > max_width ? max_width : static_cast<unsigned>(parsed);
                    out.editor.tab_width = clamped;
                }
                catch (const std::exception &)
                {
                    // Ignore malformed values and keep defaults.
                }
            }
            else if (lower_key == "soft_wrap")
            {
                out.editor.soft_wrap = parse_bool(value);
            }
        }
    }
    return true;
}

} // namespace viper::tui::config
