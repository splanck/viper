// tui/src/ui/container.cpp
// @brief Implementation of stacking container widgets.
// @invariant Children are laid out sequentially along the container's axis.
// @ownership Container owns its children and delegates painting and events.

#include "tui/ui/container.hpp"

namespace viper::tui::ui
{

Container::Container(Direction dir) : dir_(dir) {}

void Container::addChild(std::unique_ptr<Widget> child)
{
    children_.push_back(std::move(child));
}

void Container::layout(Rect rect)
{
    Widget::layout(rect);
    if (children_.empty())
    {
        return;
    }
    int count = static_cast<int>(children_.size());
    if (dir_ == Direction::Vertical)
    {
        int each = rect.h / count;
        int y = rect.y;
        for (int i = 0; i < count; ++i)
        {
            int h = (i == count - 1) ? (rect.y + rect.h - y) : each;
            children_[i]->layout({rect.x, y, rect.w, h});
            y += h;
        }
    }
    else
    {
        int each = rect.w / count;
        int x = rect.x;
        for (int i = 0; i < count; ++i)
        {
            int w = (i == count - 1) ? (rect.x + rect.w - x) : each;
            children_[i]->layout({x, rect.y, w, rect.h});
            x += w;
        }
    }
}

void Container::paint(render::ScreenBuffer &sb)
{
    for (auto &child : children_)
    {
        child->paint(sb);
    }
}

bool Container::onEvent(const Event &ev)
{
    for (auto &child : children_)
    {
        if (child->onEvent(ev))
        {
            return true;
        }
    }
    return false;
}

VStack::VStack() : Container(Direction::Vertical) {}

HStack::HStack() : Container(Direction::Horizontal) {}

} // namespace viper::tui::ui
