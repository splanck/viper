//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/util/unicode.cpp
// Purpose: Provide Unicode helpers required by the terminal UI for measuring
//          display width and decoding UTF-8 input streams.
// Key invariants: Width calculations default to 1 column and treat combining
//                 marks as zero width, matching common terminal behaviour.
// Ownership/Lifetime: Functions operate on caller-supplied views and own no
//                     persistent state.
// Links: docs/tui/rendering.md#unicode
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements UTF-8 decoding and display width classification helpers.
/// @details Rendering in a terminal requires translating UTF-8 encoded input
///          into Unicode scalar values and estimating their on-screen width.
///          These utilities provide lightweight implementations tailored to the
///          editor's needs without pulling in heavyweight ICU dependencies.

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

/// @brief Unicode ranges that occupy two terminal columns on most displays.
/// @details The table covers East Asian Wide and Fullwidth blocks.  Values not
///          captured here are assumed to consume a single column.
constexpr Range kWideRanges[] = {{0x1100, 0x115F},
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

/// @brief Estimate the terminal display width of a Unicode code point.
/// @details Treats combining diacritical marks as zero width, wide ranges from
///          @ref kWideRanges as two columns, and all other characters as single
///          column.  The heuristic mirrors wcwidth() semantics and is sufficient
///          for layout calculations in the text widgets.
/// @param cp Unicode scalar value to classify.
/// @return 0 for combining marks, 2 for wide glyphs, otherwise 1.
int char_width(char32_t cp)
{
    if (cp >= 0x0300 && cp <= 0x036F)
    {
        return 0;
    }
    for (auto range : kWideRanges)
    {
        if (cp >= range.first && cp <= range.last)
        {
            return 2;
        }
    }
    return 1;
}

/// @brief Decode a UTF-8 byte sequence into a string of Unicode scalars.
/// @details Iterates the input view, classifying the lead byte to determine the
///          sequence length, validating continuation bytes, and assembling the
///          resulting code point.  Invalid sequences, overlong encodings, or
///          surrogate code points are replaced with U+FFFD, mirroring the
///          behaviour of tolerant decoders.
/// @param in UTF-8 encoded input to decode.
/// @return std::u32string containing the decoded Unicode scalar values.
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
        bool overlong =
            (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
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
