// tui/include/tui/ui/widget.hpp
// @brief Base class for UI widgets with layout and event handling.
// @invariant Widgets manage their own rectangular bounds.
// @ownership Derived classes own their state; Widget stores layout rect.
#pragma once

#include "tui/render/screen.hpp"

namespace viper::tui::ui
{
struct Rect
{
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

struct Event
{
};

/// @brief Abstract base for all widgets.
class Widget
{
  public:
    virtual ~Widget() = default;

    /// @brief Set widget bounds and layout children.
    virtual void layout(const Rect &r);

    /// @brief Paint widget contents into a screen buffer.
    virtual void paint(render::ScreenBuffer &sb);

    /// @brief Handle an input event.
    /// @return True if the event was consumed.
    virtual bool onEvent(const Event &ev);

    /// @brief Retrieve widget rectangle.
    [[nodiscard]] Rect rect() const
    {
        return rect_;
    }

  protected:
    Rect rect_{};
};

} // namespace viper::tui::ui
