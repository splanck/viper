// tui/src/app.cpp
// @brief Headless application loop implementation.
// @invariant Renderer draws one frame per tick after processing events.
// @ownership App owns root widget and screen buffer.

#include "tui/app.hpp"

namespace viper::tui
{
App::App(
    std::unique_ptr<ui::Widget> root, ::tui::term::TermIO &tio, int rows, int cols, bool truecolor)
    : root_(std::move(root)), renderer_(tio, truecolor), rows_(rows), cols_(cols)
{
    screen_.resize(rows, cols);
}

void App::pushEvent(const ui::Event &ev)
{
    events_.push_back(ev);
}

void App::tick()
{
    for (const auto &ev : events_)
    {
        if (ev.key.key == ui::Key::Tab)
        {
            if (ev.key.shift)
            {
                focus_.prev();
            }
            else
            {
                focus_.next();
            }
            continue;
        }
        bool consumed = false;
        if (auto *f = focus_.focused())
        {
            consumed = f->onEvent(ev);
        }
        if (!consumed && root_)
        {
            root_->onEvent(ev);
        }
    }
    events_.clear();
    if (root_)
    {
        ui::Rect full{0, 0, cols_, rows_};
        root_->layout(full);
        screen_.clear(render::Style{});
        root_->paint(screen_);
    }
    renderer_.draw(screen_);
    screen_.snapshotPrev();
}

} // namespace viper::tui
