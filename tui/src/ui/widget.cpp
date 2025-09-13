// tui/src/ui/widget.cpp
// @brief Default implementations for Widget base class.
// @invariant Rect stored matches last layout call.
// @ownership Widget does not own external resources.

#include "tui/ui/widget.hpp"

namespace viper::tui::ui
{
void Widget::layout(const Rect &r)
{
    rect_ = r;
}

void Widget::paint(render::ScreenBuffer &) {}

bool Widget::onEvent(const Event &)
{
    return false;
}

bool Widget::wantsFocus() const
{
    return false;
}

} // namespace viper::tui::ui
