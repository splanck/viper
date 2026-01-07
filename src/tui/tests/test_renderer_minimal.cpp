//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_renderer_minimal.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"

#include <algorithm>
#include <cassert>
#include <string>

using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::render::Style;
using viper::tui::term::StringTermIO;

static int countChar(const std::string &s, char c)
{
    return static_cast<int>(std::count(s.begin(), s.end(), c));
}

int main()
{
    StringTermIO tio;
    Renderer r(tio, true);
    ScreenBuffer sb;
    sb.resize(2, 3);
    Style style{};
    style.fg = {255, 0, 0, 255};
    style.bg = {0, 0, 0, 255};
    sb.clear(style);
    const char *row0 = "xyz";
    const char *row1 = "uvw";
    for (int i = 0; i < 3; ++i)
    {
        sb.at(0, i).ch = row0[i];
        sb.at(1, i).ch = row1[i];
        sb.at(0, i).style = style;
        sb.at(1, i).style = style;
    }
    r.draw(sb);
    sb.snapshotPrev();
    int first_sgr = countChar(tio.buffer(), 'm');
    tio.clear();

    const char *row1b = "UVW";
    for (int i = 0; i < 3; ++i)
    {
        sb.at(1, i).ch = row1b[i];
    }
    r.draw(sb);
    int second_sgr = countChar(tio.buffer(), 'm');
    const std::string &out = tio.buffer();
    assert(second_sgr <= first_sgr);
    assert(out.find('x') == std::string::npos);
    assert(out.find('y') == std::string::npos);
    assert(out.find('z') == std::string::npos);
    return 0;
}
