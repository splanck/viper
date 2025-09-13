// tui/src/app.cpp
// @brief Implementation of the minimal application loop.
// @invariant Each tick processes all queued events exactly once.
// @ownership App owns its screen buffer and root widget.

#include "tui/app.hpp"

#include "tui/term/term_io.hpp"

namespace viper::tui
{

App::App(std::unique_ptr<ui::Widget> root, ::tui::term::TermIO &tio)
    : root_(std::move(root)), renderer_(tio, false)
{
}

void App::pushEvent(const ui::Event &ev)
{
    events_.push_back(ev);
}

void App::resize(int rows, int cols)
{
    rows_ = rows;
    cols_ = cols;
    screen_.resize(rows, cols);
}

void App::tick()
{
    for (const auto &ev : events_)
    {
        if (root_)
        {
            root_->onEvent(ev);
        }
    }
    events_.clear();
    if (root_)
    {
        root_->layout(ui::Rect{0, 0, cols_, rows_});
        render::Style style{};
        screen_.clear(style);
        root_->paint(screen_);
    }
    renderer_.draw(screen_);
    screen_.snapshotPrev();
}

} // namespace viper::tui
