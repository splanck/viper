// tui/apps/tui_demo.cpp
// @brief Small demo app wiring TerminalSession + App + a couple widgets.
// @invariant Exits on Ctrl+Q in interactive mode; headless renders once.
// @ownership App owns widget tree; TermIO and environment are borrowed.

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

int main()
{
    bool headless = false;
    if (const char *v = std::getenv("VIPERTUI_NO_TTY"))
    {
        headless = (v[0] == '1');
    }

    tui::TerminalSession session;
    tui::term::RealTermIO tio;

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
