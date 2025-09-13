// tui/src/term/input.cpp
// @brief Implements UTF-8 input decoding into key events.
// @invariant Partial UTF-8 sequences are preserved across feeds.
// @ownership Owns decoded event queue; no ownership of input bytes.

#include "tui/term/input.hpp"

#include <string_view>

namespace viper::tui::term
{

void InputDecoder::emit(uint32_t cp)
{
    KeyEvent ev{};
    switch (cp)
    {
        case '\r':
        case '\n':
            ev.code = KeyEvent::Code::Enter;
            break;
        case '\t':
            ev.code = KeyEvent::Code::Tab;
            break;
        case 0x1b:
            ev.code = KeyEvent::Code::Esc;
            break;
        case 0x7f:
            ev.code = KeyEvent::Code::Backspace;
            break;
        default:
            if (cp >= 0x20)
            {
                ev.codepoint = cp;
            }
            else
            {
                ev.code = KeyEvent::Code::Unknown;
            }
            break;
    }
    key_events_.push_back(ev);
}

static std::vector<int> parse_params(std::string_view params)
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

unsigned InputDecoder::decode_mod(int value)
{
    if (value < 2)
    {
        return 0;
    }
    return static_cast<unsigned>(value - 1);
}

void InputDecoder::handle_sgr_mouse(char final, std::string_view params)
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

InputDecoder::State InputDecoder::handle_csi(char final, std::string_view params)
{
    if ((final == 'M' || final == 'm') && !params.empty() && params.front() == '<')
    {
        handle_sgr_mouse(final, params.substr(1));
        return State::Utf8;
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
                paste_buf_.clear();
                return State::Paste;
            }
        }
        if (nums.empty())
        {
            return State::Utf8;
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
                return State::Utf8;
        }
        key_events_.push_back(ev);
        return State::Utf8;
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
            return State::Utf8;
    }
    key_events_.push_back(ev);
    return State::Utf8;
}

void InputDecoder::handle_ss3(char final, std::string_view params)
{
    auto nums = parse_params(params);
    unsigned mods = 0;
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
        case 'P':
            ev.code = KeyEvent::Code::F1;
            break;
        case 'Q':
            ev.code = KeyEvent::Code::F2;
            break;
        case 'R':
            ev.code = KeyEvent::Code::F3;
            break;
        case 'S':
            ev.code = KeyEvent::Code::F4;
            break;
        default:
            return;
    }
    key_events_.push_back(ev);
}

void InputDecoder::feed(std::string_view bytes)
{
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        unsigned char b = static_cast<unsigned char>(bytes[i]);
        switch (state_)
        {
            case State::Utf8:
                if (expected_ == 0)
                {
                    if (b == 0x1b)
                    {
                        state_ = State::Esc;
                    }
                    else if (b < 0x80)
                    {
                        emit(b);
                    }
                    else if ((b & 0xE0) == 0xC0)
                    {
                        cp_ = b & 0x1F;
                        expected_ = 1;
                    }
                    else if ((b & 0xF0) == 0xE0)
                    {
                        cp_ = b & 0x0F;
                        expected_ = 2;
                    }
                    else if ((b & 0xF8) == 0xF0)
                    {
                        cp_ = b & 0x07;
                        expected_ = 3;
                    }
                    else
                    {
                        key_events_.push_back(KeyEvent{});
                    }
                }
                else if ((b & 0xC0) == 0x80)
                {
                    cp_ = (cp_ << 6) | (b & 0x3F);
                    if (--expected_ == 0)
                    {
                        emit(cp_);
                        cp_ = 0;
                    }
                }
                else
                {
                    key_events_.push_back(KeyEvent{});
                    cp_ = 0;
                    expected_ = 0;
                    --i;
                }
                break;
            case State::Esc:
                if (b == '[')
                {
                    state_ = State::CSI;
                    seq_.clear();
                }
                else if (b == 'O')
                {
                    state_ = State::SS3;
                    seq_.clear();
                }
                else
                {
                    emit(0x1b);
                    state_ = State::Utf8;
                    --i;
                }
                break;
            case State::CSI:
                if (b >= 0x40 && b <= 0x7E)
                {
                    state_ = handle_csi(static_cast<char>(b), seq_);
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
            case State::SS3:
                if (b >= 0x40 && b <= 0x7E)
                {
                    handle_ss3(static_cast<char>(b), seq_);
                    state_ = State::Utf8;
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
            case State::Paste:
                if (b == 0x1b)
                {
                    state_ = State::PasteEsc;
                }
                else
                {
                    paste_buf_.push_back(static_cast<char>(b));
                }
                break;
            case State::PasteEsc:
                if (b == '[')
                {
                    state_ = State::PasteCSI;
                    seq_.clear();
                }
                else
                {
                    paste_buf_.push_back('\x1b');
                    paste_buf_.push_back(static_cast<char>(b));
                    state_ = State::Paste;
                }
                break;
            case State::PasteCSI:
                if (b >= 0x40 && b <= 0x7E)
                {
                    if (b == '~' && seq_ == "201")
                    {
                        PasteEvent ev{};
                        ev.text = std::move(paste_buf_);
                        paste_events_.push_back(std::move(ev));
                        paste_buf_.clear();
                        state_ = State::Utf8;
                    }
                    else
                    {
                        paste_buf_.append("\x1b[");
                        paste_buf_.append(seq_);
                        paste_buf_.push_back(static_cast<char>(b));
                        state_ = State::Paste;
                    }
                    seq_.clear();
                }
                else
                {
                    seq_.push_back(static_cast<char>(b));
                }
                break;
        }
    }
}

std::vector<KeyEvent> InputDecoder::drain()
{
    auto out = std::move(key_events_);
    key_events_.clear();
    return out;
}

std::vector<MouseEvent> InputDecoder::drain_mouse()
{
    auto out = std::move(mouse_events_);
    mouse_events_.clear();
    return out;
}

std::vector<PasteEvent> InputDecoder::drain_paste()
{
    auto out = std::move(paste_events_);
    paste_events_.clear();
    return out;
}

} // namespace viper::tui::term
