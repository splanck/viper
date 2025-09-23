// tui/src/term/CsiParser.cpp
// @brief Implements parsing of terminal CSI sequences into events.
// @invariant Produces key and mouse events according to VT conventions.
// @ownership Writes into caller-supplied event buffers.

#include "tui/term/CsiParser.hpp"

namespace viper::tui::term
{

CsiParser::CsiParser(std::vector<KeyEvent>& keys,
                     std::vector<MouseEvent>& mouse,
                     std::string& paste_buffer)
    : key_events_(keys)
    , mouse_events_(mouse)
    , paste_buffer_(paste_buffer)
{
}

std::vector<int> CsiParser::parse_params(std::string_view params) const
{
    std::vector<int> out;
    int val = 0;
    bool have = false;
    for (char c : params)
    {
        if (c >= '0' && c <= '9')
        {
            val = val * 10 + (c - '0');
            have = true;
        }
        else if (c == ';')
        {
            out.push_back(have ? val : 0);
            val = 0;
            have = false;
        }
    }
    if (have)
    {
        out.push_back(val);
    }
    return out;
}

unsigned CsiParser::decode_mod(int value) const
{
    if (value < 2)
    {
        return 0;
    }
    return static_cast<unsigned>(value - 1);
}

void CsiParser::handle_sgr_mouse(char final, std::string_view params)
{
    auto nums = parse_params(params);
    if (nums.size() < 3)
    {
        return;
    }
    int b = nums[0];
    MouseEvent ev{};
    ev.x = nums[1] - 1;
    ev.y = nums[2] - 1;
    if (b & 4)
    {
        ev.mods |= KeyEvent::Shift;
    }
    if (b & 8)
    {
        ev.mods |= KeyEvent::Alt;
    }
    if (b & 16)
    {
        ev.mods |= KeyEvent::Ctrl;
    }

    if ((b >= 64 && b <= 65) || (b >= 96 && b <= 97))
    {
        ev.type = MouseEvent::Type::Wheel;
        ev.buttons = (b & 1) ? 2u : 1u;
    }
    else if (final == 'm')
    {
        ev.type = MouseEvent::Type::Up;
        ev.buttons = 1u << (b & 3);
    }
    else if (b & 32)
    {
        ev.type = MouseEvent::Type::Move;
        ev.buttons = 1u << (b & 3);
    }
    else
    {
        ev.type = MouseEvent::Type::Down;
        ev.buttons = 1u << (b & 3);
    }
    mouse_events_.push_back(ev);
}

CsiResult CsiParser::handle(char final, std::string_view params)
{
    CsiResult result{};
    if ((final == 'M' || final == 'm') && !params.empty() && params.front() == '<')
    {
        handle_sgr_mouse(final, params.substr(1));
        result.handled = true;
        return result;
    }

    auto nums = parse_params(params);
    unsigned mods = 0;
    if (final == '~')
    {
        if (nums.size() >= 2)
        {
            mods = decode_mod(nums[1]);
        }
        if (!nums.empty())
        {
            if (nums[0] == 200)
            {
                paste_buffer_.clear();
                result.start_paste = true;
                result.handled = true;
                return result;
            }
        }
        if (nums.empty())
        {
            return result;
        }
        KeyEvent ev{};
        ev.mods = mods;
        switch (nums[0])
        {
            case 1:
                ev.code = KeyEvent::Code::Home;
                break;
            case 2:
                ev.code = KeyEvent::Code::Insert;
                break;
            case 3:
                ev.code = KeyEvent::Code::Delete;
                break;
            case 4:
                ev.code = KeyEvent::Code::End;
                break;
            case 5:
                ev.code = KeyEvent::Code::PageUp;
                break;
            case 6:
                ev.code = KeyEvent::Code::PageDown;
                break;
            case 11:
                ev.code = KeyEvent::Code::F1;
                break;
            case 12:
                ev.code = KeyEvent::Code::F2;
                break;
            case 13:
                ev.code = KeyEvent::Code::F3;
                break;
            case 14:
                ev.code = KeyEvent::Code::F4;
                break;
            case 15:
                ev.code = KeyEvent::Code::F5;
                break;
            case 17:
                ev.code = KeyEvent::Code::F6;
                break;
            case 18:
                ev.code = KeyEvent::Code::F7;
                break;
            case 19:
                ev.code = KeyEvent::Code::F8;
                break;
            case 20:
                ev.code = KeyEvent::Code::F9;
                break;
            case 21:
                ev.code = KeyEvent::Code::F10;
                break;
            case 23:
                ev.code = KeyEvent::Code::F11;
                break;
            case 24:
                ev.code = KeyEvent::Code::F12;
                break;
            default:
                return result;
        }
        key_events_.push_back(ev);
        result.handled = true;
        return result;
    }

    if (nums.size() >= 2)
    {
        mods = decode_mod(nums[1]);
    }

    KeyEvent ev{};
    ev.mods = mods;
    switch (final)
    {
        case 'A':
            ev.code = KeyEvent::Code::Up;
            break;
        case 'B':
            ev.code = KeyEvent::Code::Down;
            break;
        case 'C':
            ev.code = KeyEvent::Code::Right;
            break;
        case 'D':
            ev.code = KeyEvent::Code::Left;
            break;
        case 'H':
            ev.code = KeyEvent::Code::Home;
            break;
        case 'F':
            ev.code = KeyEvent::Code::End;
            break;
        default:
            return result;
    }
    key_events_.push_back(ev);
    result.handled = true;
    return result;
}

} // namespace viper::tui::term

