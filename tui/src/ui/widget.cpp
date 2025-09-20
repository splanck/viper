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

/// @brief Default widgets decline focus so derived classes must opt in.
bool Widget::wantsFocus() const
{
    return false;
}

/// @brief Notify derived classes of focus transitions; base implementation ignores them.
void Widget::onFocusChanged(bool)
{
}

/// @brief Retrieve the rectangle describing the widget's layout slot.
Rect Widget::rect() const
{
    return rect_;
}

} // namespace viper::tui::ui
