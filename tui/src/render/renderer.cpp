// tui/src/render/renderer.cpp
// @brief Implementation of ANSI renderer emitting minimal diff sequences.
// @invariant Avoids redundant SGR by tracking current style.
// @ownership Renderer holds a reference to external TermIO and borrows ScreenBuffer.

#include "tui/render/renderer.hpp"

#include <string>
#include <vector>

using ::tui::term::TermIO;

namespace viper::tui::render
{
namespace
{
std::string encodeUTF8(char32_t cp)
{
    std::string out;
    if (cp <= 0x7F)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

int rgbTo256(const RGBA &c)
{
    auto map = [](uint8_t v) { return static_cast<int>(v / 51); };
    return 16 + 36 * map(c.r) + 6 * map(c.g) + map(c.b);
}
} // namespace

Renderer::Renderer(TermIO &io, bool truecolor) : io_(io), truecolor_(truecolor) {}

void Renderer::moveCursor(int y, int x)
{
    std::string seq = "\x1b[";
    seq += std::to_string(y + 1);
    seq.push_back(';');
    seq += std::to_string(x + 1);
    seq.push_back('H');
    io_.write(seq);
}

void Renderer::setStyle(Style style)
{
    if (haveCurr_ && style == curr_)
    {
        return;
    }

    std::string seq = "\x1b[";
    bool first = true;
    auto append = [&](int n)
    {
        if (!first)
        {
            seq.push_back(';');
        }
        seq += std::to_string(n);
        first = false;
    };

    if (style.attrs & Bold)
        append(1);
    if (style.attrs & Faint)
        append(2);
    if (style.attrs & Italic)
        append(3);
    if (style.attrs & Underline)
        append(4);
    if (style.attrs & Blink)
        append(5);
    if (style.attrs & Reverse)
        append(7);
    if (style.attrs & Invisible)
        append(8);
    if (style.attrs & Strike)
        append(9);

    auto addColor = [&](bool fg, const RGBA &c)
    {
        append(fg ? 38 : 48);
        if (truecolor_)
        {
            append(2);
            append(c.r);
            append(c.g);
            append(c.b);
        }
        else
        {
            append(5);
            append(rgbTo256(c));
        }
    };

    addColor(true, style.fg);
    addColor(false, style.bg);

    if (first)
    {
        seq += '0';
    }
    seq.push_back('m');
    io_.write(seq);
    curr_ = style;
    haveCurr_ = true;
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
            io_.write(encodeUTF8(cell.ch));
        }
    }
    io_.flush();
}

} // namespace viper::tui::render
