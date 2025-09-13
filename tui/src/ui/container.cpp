// tui/src/ui/container.cpp
// @brief Container widget implementations for vertical and horizontal stacks.
// @invariant Child layouts cover container rect without overlap.
// @ownership Container owns child widgets and delegates painting.

#include "tui/ui/container.hpp"

namespace viper::tui::ui
{
void Container::addChild(std::unique_ptr<Widget> child)
{
    children_.push_back(std::move(child));
}

void Container::layout(const Rect &r)
{
    Widget::layout(r);
    layoutChildren();
}

void Container::paint(render::ScreenBuffer &sb)
{
    for (auto &ch : children_)
    {
        ch->paint(sb);
    }
}

void VStack::layoutChildren()
{
    int count = static_cast<int>(children_.size());
    if (count == 0)
    {
        return;
    }
    int base = rect_.h / count;
    int rem = rect_.h - base * count;
    int y = rect_.y;
    for (int i = 0; i < count; ++i)
    {
        int h = base + (i == count - 1 ? rem : 0);
        Rect cr{rect_.x, y, rect_.w, h};
        children_[i]->layout(cr);
        y += h;
    }
}

void HStack::layoutChildren()
{
    int count = static_cast<int>(children_.size());
    if (count == 0)
    {
        return;
    }
    int base = rect_.w / count;
    int rem = rect_.w - base * count;
    int x = rect_.x;
    for (int i = 0; i < count; ++i)
    {
        int w = base + (i == count - 1 ? rem : 0);
        Rect cr{x, rect_.y, w, rect_.h};
        children_[i]->layout(cr);
        x += w;
    }
}

} // namespace viper::tui::ui
