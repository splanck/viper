//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/util/unicode.cpp
// Purpose: Provide Unicode utilities for width computation and UTF-8 decoding
//          used by the Viper TUI text rendering subsystem.
// Key invariants: Routines never allocate more than the returned containers,
//                 operate on well-formed UTF-8 without invoking undefined
//                 behaviour, and gracefully substitute replacement characters
//                 for malformed sequences.
// Ownership/Lifetime: Functions borrow input views and return value-owned
//                     containers so callers retain full control over storage.
// Links: docs/architecture.md#vipertui-architecture
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements Unicode width classification and UTF-8 decoding helpers.
/// @details Rendering widgets need to reason about East Asian full-width code
///          points and robustly decode arbitrary UTF-8 user input.  This file
///          consolidates the lookup tables and decoding loop so every widget
///          relies on the same behaviour for combining marks, overlong encodings,
///          and invalid byte sequences.

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

/// @brief Compute the terminal column width of a Unicode code point.
/// @details Treats combining marks in the U+0300–U+036F range as zero-width and
///          recognises East Asian full-width ranges listed in @ref wide_ranges.
///          The helper performs only integer comparisons, making it suitable for
///          per-code-point queries within rendering hot paths.  Code points that
///          fall outside the known wide ranges default to width @c 1, matching
///          common terminal behaviour.
/// @param cp Unicode scalar value to classify.
/// @return @c 0 for combining marks, @c 2 for full-width glyphs, otherwise @c 1.
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

/// @brief Decode a UTF-8 byte sequence into a UTF-32 string.
/// @details Iterates through @p in using a hand-written state machine that
///          handles one-, two-, three-, and four-byte UTF-8 sequences.  Invalid
///          headers, truncated continuation bytes, surrogate halves, or overlong
///          encodings trigger insertion of U+FFFD while advancing by a single
///          byte so the decoder makes forward progress.  Successful decodes
///          append the resulting scalar value to the output string.  The
///          resulting container owns its storage, leaving callers free to retain
///          the decoded content independently of the input view.
/// @param in UTF-8 encoded byte span to convert.
/// @return UTF-32 string containing decoded scalar values with replacement
///         characters substituted for malformed regions.
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
