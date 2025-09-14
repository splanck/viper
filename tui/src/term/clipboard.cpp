// tui/src/term/clipboard.cpp
// @brief Implements OSC 52 clipboard operations.
// @invariant Respects VIPERTUI_DISABLE_OSC52 environment guard.
// @ownership Osc52Clipboard borrows TermIO; MockClipboard stores sequence.

#include "tui/term/clipboard.hpp"
#include "tui/term/term_io.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

namespace tui::term
{
namespace
{
static const char kB64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(std::string_view in)
{
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size())
    {
        unsigned triple = (static_cast<unsigned char>(in[i]) << 16) |
                          (static_cast<unsigned char>(in[i + 1]) << 8) |
                          (static_cast<unsigned char>(in[i + 2]));
        out.push_back(kB64Table[(triple >> 18) & 0x3F]);
        out.push_back(kB64Table[(triple >> 12) & 0x3F]);
        out.push_back(kB64Table[(triple >> 6) & 0x3F]);
        out.push_back(kB64Table[triple & 0x3F]);
        i += 3;
    }
    if (i < in.size())
    {
        unsigned triple = static_cast<unsigned char>(in[i]) << 16;
        bool two = false;
        if (i + 1 < in.size())
        {
            triple |= static_cast<unsigned char>(in[i + 1]) << 8;
            two = true;
        }
        out.push_back(kB64Table[(triple >> 18) & 0x3F]);
        out.push_back(kB64Table[(triple >> 12) & 0x3F]);
        if (two)
        {
            out.push_back(kB64Table[(triple >> 6) & 0x3F]);
            out.push_back('=');
        }
        else
        {
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

std::string build_seq(std::string_view text)
{
    std::string seq("\x1b]52;c;");
    seq += base64_encode(text);
    seq.push_back('\x07');
    return seq;
}

bool osc52_disabled()
{
    const char *v = std::getenv("VIPERTUI_DISABLE_OSC52");
    return v && v[0] == '1';
}

} // namespace

Osc52Clipboard::Osc52Clipboard(TermIO &io) : io_(io) {}

bool Osc52Clipboard::copy(std::string_view text)
{
    if (osc52_disabled())
    {
        return false;
    }
    io_.write(build_seq(text));
    io_.flush();
    return true;
}

std::string Osc52Clipboard::paste()
{
    return {};
}

bool MockClipboard::copy(std::string_view text)
{
    if (osc52_disabled())
    {
        last_.clear();
        return false;
    }
    last_ = build_seq(text);
    return true;
}

std::string MockClipboard::paste()
{
    // Expect: ESC ] 52 ; c ; <base64> BEL   (or ST terminator)
    auto find_last_semicolon = last_.rfind(';');
    if (find_last_semicolon == std::string::npos)
        return {};

    // Find terminator: BEL (\x07) or ST (\x1b\\)
    std::size_t end = last_.find('\x07', find_last_semicolon + 1);
    if (end == std::string::npos)
    {
        // Look for ST: ESC \\ terminator
        std::size_t esc = last_.find('\x1b', find_last_semicolon + 1);
        if (esc != std::string::npos && esc + 1 < last_.size() && last_[esc + 1] == '\\')
        {
            end = esc;
        }
    }
    if (end == std::string::npos)
        return {};

    std::string_view b64 =
        std::string_view(last_).substr(find_last_semicolon + 1, end - (find_last_semicolon + 1));

    auto dec = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        if (c == '=')
            return -2; // padding
        return -1;     // invalid
    };

    std::string out;
    int val = 0;
    int valb = -8;
    for (char c : b64)
    {
        int d = dec(c);
        if (d < 0)
        {
            if (d == -2)
                break; // stop at padding
            continue;  // skip invalid
        }
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

} // namespace tui::term
