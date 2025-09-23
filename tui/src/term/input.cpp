// tui/src/term/input.cpp
// @brief Implements UTF-8 input decoding into terminal events.
// @invariant Partial UTF-8 sequences are preserved across feeds.
// @ownership Owns decoded event queues; no ownership of input bytes.

#include "tui/term/input.hpp"

#include <string_view>

namespace viper::tui::term
{

InputDecoder::InputDecoder()
    : csi_parser_(key_events_, mouse_events_, paste_buf_)
{
}

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

InputDecoder::State InputDecoder::handle_csi(char final, std::string_view params)
{
    auto result = csi_parser_.handle(final, params);
    if (result.start_paste)
    {
        return State::Paste;
    }
    return State::Utf8;
}

void InputDecoder::handle_ss3(char final, std::string_view params)
{
    auto nums = csi_parser_.parse_params(params);
    unsigned mods = 0;
    if (nums.size() >= 2)
    {
        mods = csi_parser_.decode_mod(nums[1]);
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
            {
                if (utf8_decoder_.idle() && b == 0x1b)
                {
                    state_ = State::Esc;
                }
                else
                {
                    Utf8Result result = utf8_decoder_.feed(b);
                    if (result.has_codepoint)
                    {
                        emit(result.codepoint);
                    }
                    if (result.error)
                    {
                        key_events_.push_back(KeyEvent{});
                    }
                    if (result.replay)
                    {
                        --i;
                    }
                }
                break;
            }
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

