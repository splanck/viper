// tui/src/term/input.cpp
// @brief Implements UTF-8 input decoding into key events.
// @invariant Partial UTF-8 sequences are preserved across feeds.
// @ownership Owns decoded event queue; no ownership of input bytes.

#include "tui/term/input.hpp"

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
    events_.push_back(ev);
}

void InputDecoder::feed(std::string_view bytes)
{
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        unsigned char b = static_cast<unsigned char>(bytes[i]);
        if (expected_ == 0)
        {
            if (b < 0x80)
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
                events_.push_back(KeyEvent{});
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
            events_.push_back(KeyEvent{});
            cp_ = 0;
            expected_ = 0;
            --i; // reprocess this byte
        }
    }
}

std::vector<KeyEvent> InputDecoder::drain()
{
    auto out = std::move(events_);
    events_.clear();
    return out;
}

} // namespace viper::tui::term
