// tui/src/util/unicode.cpp
// @brief Implements Unicode width and UTF-8 decoding helpers.
// @invariant Width table covers common ranges; others default to width 1.
// @ownership No ownership beyond returned strings.

#include "tui/util/unicode.hpp"

namespace viper::tui::util
{
namespace
{
struct Range
{
    char32_t first;
    char32_t last;
};

constexpr Range wide_ranges[] = {{0x1100, 0x115F},
                                 {0x2329, 0x232A},
                                 {0x2E80, 0xA4CF},
                                 {0xAC00, 0xD7A3},
                                 {0xF900, 0xFAFF},
                                 {0xFE10, 0xFE19},
                                 {0xFE30, 0xFE6F},
                                 {0xFF00, 0xFF60},
                                 {0xFFE0, 0xFFE6},
                                 {0x20000, 0x2FFFD},
                                 {0x30000, 0x3FFFD}};
} // namespace

int char_width(char32_t cp)
{
    if (cp >= 0x0300 && cp <= 0x036F)
    {
        return 0;
    }
    for (auto r : wide_ranges)
    {
        if (cp >= r.first && cp <= r.last)
        {
            return 2;
        }
    }
    return 1;
}

std::u32string decode_utf8(std::string_view in)
{
    std::u32string out;
    for (size_t i = 0; i < in.size();)
    {
        unsigned char c = static_cast<unsigned char>(in[i]);
        char32_t cp = 0;
        int len = 0;
        if (c < 0x80)
        {
            cp = c;
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            len = 4;
        }
        else
        {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + static_cast<size_t>(len) > in.size())
        {
            out.push_back(0xFFFD);
            break;
        }
        bool ok = true;
        for (int j = 1; j < len; ++j)
        {
            unsigned char cc = static_cast<unsigned char>(in[i + j]);
            if ((cc & 0xC0) != 0x80)
            {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok)
        {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

} // namespace viper::tui::util
