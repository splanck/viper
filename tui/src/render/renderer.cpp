// tui/src/render/renderer.cpp
// @brief Implementation of ANSI renderer producing minimal terminal updates.
// @invariant setStyle and moveCursor avoid redundant sequences based on cached state.
// @ownership Renderer writes through a borrowed TermIO reference.

#include "tui/render/renderer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace viper::tui::render
{

Renderer::Renderer(::viper::tui::term::TermIO &tio, bool truecolor) : tio_(tio), truecolor_(truecolor) {}

namespace
{
int toCube(uint8_t c)
{
    return c / 51; // map 0-255 to 0-5
}
} // namespace

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
