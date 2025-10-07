// tui/src/config/config.cpp
// @brief INI-like configuration loader implementation.
// @invariant Reads sections [theme], [keymap.global], and [editor].
// @ownership Loader does not own external resources beyond file path.

#include "tui/config/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace viper::tui::config
{

namespace
{
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

bool parse_color(const std::string &s, render::RGBA &out)
{
    std::string hex = s;
    if (!hex.empty() && hex[0] == '#')
    {
        hex.erase(0, 1);
    }
    if (hex.size() != 6)
    {
        return false;
    }
    unsigned v = 0;
    std::istringstream iss(hex);
    iss >> std::hex >> v;
    if (iss.fail())
    {
        return false;
    }
    out.r = static_cast<uint8_t>((v >> 16) & 0xFF);
    out.g = static_cast<uint8_t>((v >> 8) & 0xFF);
    out.b = static_cast<uint8_t>(v & 0xFF);
    out.a = 255;
    return true;
}

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
        const std::string suffix = name.substr(1);
        try
        {
            size_t parsed = 0;
            const int num = std::stoi(suffix, &parsed);
            if (parsed != suffix.size())
            {
                return Code::Unknown;
            }
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
                    return Code::Unknown;
            }
        }
        catch (const std::invalid_argument &)
        {
            return Code::Unknown;
        }
        catch (const std::out_of_range &)
        {
            return Code::Unknown;
        }
    }
    return Code::Unknown;
}

input::KeyChord parse_chord(const std::string &str)
{
    input::KeyChord kc{};
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, '+'))
    {
        token = trim(token);
        std::string lower;
        lower.resize(token.size());
        std::transform(token.begin(),
                       token.end(),
                       lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
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

bool parse_bool(const std::string &s)
{
    std::string lower;
    lower.resize(s.size());
    std::transform(
        s.begin(), s.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower == "1" || lower == "true" || lower == "yes";
}

} // namespace

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
            std::transform(section.begin(),
                           section.end(),
                           section.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            continue;
        }
        auto eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));
        std::string lower_key;
        lower_key.resize(key.size());
        std::transform(key.begin(),
                       key.end(),
                       lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (section == "theme")
        {
            render::RGBA col;
            if (!parse_color(value, col))
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
                out.editor.tab_width = static_cast<unsigned>(std::stoul(value));
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
