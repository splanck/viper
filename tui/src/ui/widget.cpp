// tui/src/ui/widget.cpp
// @brief Default implementations for Widget base class.
// @invariant Bounds are stored as provided to layout().
// @ownership Widget does not own external resources.

#include "tui/ui/widget.hpp"

namespace viper::tui::ui
{

void Widget::layout(Rect rect)
{
    bounds_ = rect;
}

void Widget::paint(render::ScreenBuffer &)
{
    // default no-op
}

bool Widget::onEvent(const Event &)
{
    return false;
}

} // namespace viper::tui::ui
