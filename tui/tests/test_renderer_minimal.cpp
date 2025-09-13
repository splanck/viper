// tui/tests/test_renderer_minimal.cpp
// @brief Ensure renderer emits minimal SGR sequences on diff draws.
// @invariant Unchanged regions produce no ANSI output.
// @ownership StringTermIO owns its internal buffer for verification.

#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"

#include <cassert>
#include <string>

using tui::term::StringTermIO;
using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::render::Style;

static int countSGR(const std::string &s)
{
    int c = 0;
    for (char ch : s)
    {
        if (ch == 'm')
        {
            ++c;
        }
    }
    return c;
}

int main()
{
    ScreenBuffer sb;
    sb.resize(2, 3);
    Style style{};
    sb.clear(style);
    sb.snapshotPrev();

    const char *row0 = "abc";
    const char *row1 = "xyz";
    for (int i = 0; i < 3; ++i)
    {
        sb.at(0, i).ch = static_cast<char32_t>(row0[i]);
        sb.at(1, i).ch = static_cast<char32_t>(row1[i]);
    }

    StringTermIO tio;
    Renderer renderer(tio, true);

    renderer.draw(sb);
    int first = countSGR(tio.buffer());
    assert(first > 0);
    tio.clear();
    sb.snapshotPrev();

    sb.at(0, 1).ch = U'Z';

    renderer.draw(sb);
    int second = countSGR(tio.buffer());
    assert(second < first);

    return 0;
}
