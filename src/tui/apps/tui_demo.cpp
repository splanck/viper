//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/apps/tui_demo.cpp
// Purpose: Provide a lightweight demo that wires the terminal session, widgets,
//          and renderer together for quick manual testing.
// Key invariants: The application exits on Ctrl+Q when running interactively
//                 and performs a single render when headless.
// Ownership/Lifetime: The App instance owns the widget tree while borrowing
//                     terminal I/O objects and environment data.
// Links: docs/tools.md#tui-demo
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Standalone binary demonstrating how to assemble a minimal ViperTUI
///        application.
/// @details The executable builds a widget hierarchy consisting of a text view
///          and a list view, registers them with the focus manager, and then
///          exercises the event loop.  It supports headless rendering via the
///          @c VIPERTUI_NO_TTY environment toggle to keep the demo scriptable
///          during CI runs.

#include "tui/app.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/input.hpp"
#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/views/text_view.hpp"
#include "tui/widgets/list_view.hpp"
#include "tui/widgets/splitter.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#else
#include <unistd.h>
#endif

/// @brief Entry point that constructs and runs the TUI demonstration app.
/// @details The routine wires up a @ref viper::tui::TerminalSession,
///          @ref viper::tui::App, and a pair of widgets showing a simple text
///          buffer and list.  When the @c VIPERTUI_NO_TTY environment variable
///          is set to ``1`` the demo performs a single render and exits.  When
///          interactive, keyboard events are decoded until Ctrl+Q is received.
/// @return Zero on success after the application exits.
int main()
{
    bool headless = false;
    if (const char *v = std::getenv("VIPERTUI_NO_TTY"))
    {
        headless = (v[0] == '1');
    }

    viper::tui::TerminalSession session;
    viper::tui::term::RealTermIO tio;

    viper::tui::style::Theme theme;
    viper::tui::text::TextBuffer buf;
    buf.load("Hello from ViperTUI demo\nPress Ctrl+Q to quit.");
    auto tv = std::make_unique<viper::tui::views::TextView>(buf, theme, false);
    auto *tv_ptr = tv.get();
    std::vector<std::string> items{"Item 1", "Item 2", "Item 3"};
    auto lv = std::make_unique<viper::tui::widgets::ListView>(items, theme);
    auto *lv_ptr = lv.get();
    auto root =
        std::make_unique<viper::tui::widgets::HSplitter>(std::move(tv), std::move(lv), 0.5F);

    viper::tui::App app(std::move(root), tio, 24, 80);
    app.focus().registerWidget(tv_ptr);
    app.focus().registerWidget(lv_ptr);

    app.tick();
    if (headless)
    {
        return 0;
    }

    viper::tui::term::InputDecoder decoder;
#if defined(_WIN32)
    while (true)
    {
        int c = _getch();
        if (c == 17)
        {
            break; // Ctrl+Q
        }
        char ch = static_cast<char>(c);
        decoder.feed(std::string_view(&ch, 1));
        for (auto &ev : decoder.drain())
        {
            viper::tui::ui::Event e{};
            e.key = ev;
            app.pushEvent(e);
        }
        app.tick();
    }
#else
    char in[64];
    while (true)
    {
        ssize_t n = ::read(STDIN_FILENO, in, sizeof(in));
        if (n <= 0)
        {
            break;
        }
        bool quit = false;
        for (ssize_t i = 0; i < n; ++i)
        {
            if (in[i] == 17)
            {
                quit = true; // Ctrl+Q
            }
        }
        decoder.feed(std::string_view(in, static_cast<size_t>(n)));
        for (auto &ev : decoder.drain())
        {
            viper::tui::ui::Event e{};
            e.key = ev;
            app.pushEvent(e);
        }
        app.tick();
        if (quit)
        {
            break;
        }
    }
#endif

    return 0;
}
