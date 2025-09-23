// tui/src/term/Utf8Decoder.cpp
// @brief Implements incremental UTF-8 decoding utilities.
// @invariant Maintains continuation expectations across bytes.
// @ownership Owns decoder state only.

#include "tui/term/Utf8Decoder.hpp"

namespace viper::tui::term
{

Utf8Result Utf8Decoder::feed(unsigned char byte) noexcept
{
    Utf8Result result{};
    if (expected_ == 0)
    {
        if (byte < 0x80)
        {
            result.has_codepoint = true;
            result.codepoint = byte;
        }
        else if ((byte & 0xE0) == 0xC0)
        {
            cp_ = byte & 0x1F;
            expected_ = 1;
        }
        else if ((byte & 0xF0) == 0xE0)
        {
            cp_ = byte & 0x0F;
            expected_ = 2;
        }
        else if ((byte & 0xF8) == 0xF0)
        {
            cp_ = byte & 0x07;
            expected_ = 3;
        }
        else
        {
            result.error = true;
            reset();
        }
    }
    else if ((byte & 0xC0) == 0x80)
    {
        cp_ = (cp_ << 6) | (byte & 0x3F);
        if (--expected_ == 0)
        {
            result.has_codepoint = true;
            result.codepoint = cp_;
            cp_ = 0;
        }
    }
    else
    {
        result.error = true;
        result.replay = true;
        reset();
    }
    return result;
}

bool Utf8Decoder::idle() const noexcept
{
    return expected_ == 0;
}

void Utf8Decoder::reset() noexcept
{
    cp_ = 0;
    expected_ = 0;
}

} // namespace viper::tui::term

