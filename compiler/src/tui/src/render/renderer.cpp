//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/render/renderer.cpp
// Purpose: Emit ANSI escape sequences that update the terminal to match the
//          desired screen buffer contents.
// Key invariants: Style and cursor commands are deduplicated using cached state
//                 so that redundant escape codes are not transmitted.
// Ownership/Lifetime: Renderer retains references to @ref term::TermIO but does
//                     not own the underlying terminal resources.
// Links: docs/tools.md#rendering
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the ANSI renderer that translates screen buffers into
///        terminal output.
/// @details The renderer batches diff spans produced by
///          @ref viper::tui::render::ScreenBuffer, emits the minimal cursor and
///          style changes, and streams UTF-8 glyphs directly to the terminal.
///          Helpers in this file encapsulate colour quantisation for palette
///          mode and state caching logic for escape sequences.

#include "tui/render/renderer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace viper::tui::render
{

/// @brief Construct a renderer that writes escape sequences to a terminal.
/// @param tio Terminal I/O surface used for writing and flushing bytes.
/// @param truecolor Whether to emit 24-bit colour (true) or fall back to the
///        256-colour cube approximation.
Renderer::Renderer(::viper::tui::term::TermIO &tio, bool truecolor)
    : tio_(tio), truecolor_(truecolor)
{
}

namespace
{
/// @brief Quantise an 8-bit colour channel to the six-step ANSI colour cube.
/// @details 256-colour terminals represent RGB values using a 6×6×6 cube.  Each
///          channel must be converted to a 0–5 index by dividing by 51.
/// @param c Colour channel in the range [0, 255].
/// @return Index within the cube for @p c.
int toCube(uint8_t c)
{
    return c / 51; // map 0-255 to 0-5
}
} // namespace

/// @brief Ensure the terminal style matches the requested cell attributes.
/// @details The renderer caches the last style sent to the terminal so it can
///          bail out early when no change is necessary.  Otherwise it composes
///          an SGR sequence using either truecolour or indexed palette escapes
///          before writing it through the terminal interface.
/// @param style Requested attributes for subsequent glyphs.
void Renderer::setStyle(Style style)
{
    if (style == currentStyle_)
    {
        return;
    }

    std::string seq = "\x1b[0";
    if (style.attrs & Bold)
        seq += ";1";
    if (style.attrs & Faint)
        seq += ";2";
    if (style.attrs & Italic)
        seq += ";3";
    if (style.attrs & Underline)
        seq += ";4";
    if (style.attrs & Blink)
        seq += ";5";
    if (style.attrs & Reverse)
        seq += ";7";
    if (style.attrs & Invisible)
        seq += ";8";
    if (style.attrs & Strike)
        seq += ";9";

    if (truecolor_)
    {
        seq += ";38;2;" + std::to_string(style.fg.r) + ";" + std::to_string(style.fg.g) + ";" +
               std::to_string(style.fg.b);
        seq += ";48;2;" + std::to_string(style.bg.r) + ";" + std::to_string(style.bg.g) + ";" +
               std::to_string(style.bg.b);
    }
    else
    {
        int idx_fg = 16 + 36 * toCube(style.fg.r) + 6 * toCube(style.fg.g) + toCube(style.fg.b);
        int idx_bg = 16 + 36 * toCube(style.bg.r) + 6 * toCube(style.bg.g) + toCube(style.bg.b);
        seq += ";38;5;" + std::to_string(idx_fg);
        seq += ";48;5;" + std::to_string(idx_bg);
    }

    seq += 'm';
    tio_.write(seq);
    currentStyle_ = style;
}

/// @brief Move the terminal cursor to the provided coordinates.
/// @details Cursor state is cached so the renderer avoids emitting redundant
///          positioning commands when the cursor already resides at the target.
///          Coordinates are translated to one-based terminal positions prior to
///          writing the ``CSI row;col H`` escape.
/// @param y Zero-based row to place the cursor.
/// @param x Zero-based column to place the cursor.
void Renderer::moveCursor(int y, int x)
{
    if (y == cursorY_ && x == cursorX_)
    {
        return;
    }
    std::string seq = "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + 'H';
    tio_.write(seq);
    cursorY_ = y;
    cursorX_ = x;
}

/// @brief Apply the differences from a screen buffer to the terminal.
/// @details Computes a list of modified spans, iterates over each cell, and
///          streams the UTF-8 encoding of the glyph while maintaining cursor and
///          style state.  After processing all spans the terminal output is
///          flushed to ensure prompt rendering.
/// @param sb Source buffer describing the desired screen contents.
void Renderer::draw(const ScreenBuffer &sb)
{
    std::vector<ScreenBuffer::DiffSpan> spans;
    sb.computeDiff(spans);
    for (const auto &span : spans)
    {
        moveCursor(span.row, span.x0);
        for (int x = span.x0; x < span.x1; ++x)
        {
            const Cell &cell = sb.at(span.row, x);
            setStyle(cell.style);
            char32_t ch = cell.ch;
            char buf[5];
            int len = 0;
            if (ch <= 0x7F)
            {
                buf[0] = static_cast<char>(ch);
                len = 1;
            }
            else if (ch <= 0x7FF)
            {
                buf[0] = static_cast<char>(0xC0 | ((ch >> 6) & 0x1F));
                buf[1] = static_cast<char>(0x80 | (ch & 0x3F));
                len = 2;
            }
            else if (ch <= 0xFFFF)
            {
                buf[0] = static_cast<char>(0xE0 | ((ch >> 12) & 0x0F));
                buf[1] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                buf[2] = static_cast<char>(0x80 | (ch & 0x3F));
                len = 3;
            }
            else
            {
                buf[0] = static_cast<char>(0xF0 | ((ch >> 18) & 0x07));
                buf[1] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                buf[2] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                buf[3] = static_cast<char>(0x80 | (ch & 0x3F));
                len = 4;
            }
            tio_.write(std::string_view(buf, static_cast<size_t>(len)));
            cursorX_ += cell.width;
        }
    }
    tio_.flush();
}

} // namespace viper::tui::render
