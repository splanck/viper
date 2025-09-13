// tui/include/tui/ui/widget.hpp
// @brief Base UI widget interface and supporting geometry/event types.
// @invariant layout() stores bounds for paint and event handling.
// @ownership Widgets are owned via unique_ptr within containers and App.
#pragma once

#include "tui/render/screen.hpp"

namespace viper::tui::ui
{

/// @brief Axis-aligned rectangle in screen coordinates.
struct Rect
{
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

/// @brief Generic event placeholder processed by widgets.
struct Event
{
};

/// @brief Base class for all widgets.
class Widget
{
  public:
    virtual ~Widget() = default;

    /// @brief Receive layout rectangle for this widget.
    /// @param rect Bounds allocated by parent container.
    virtual void layout(Rect rect);

    /// @brief Paint the widget into a screen buffer.
    /// @param sb Target screen buffer to modify.
    virtual void paint(render::ScreenBuffer &sb);

    /// @brief Handle an input event.
    /// @param ev Event to process.
    /// @return True if event was handled.
    virtual bool onEvent(const Event &ev);

    /// @brief Bounds assigned during layout.
    [[nodiscard]] Rect bounds() const
    {
        return bounds_;
    }

  protected:
    Rect bounds_{};
};

} // namespace viper::tui::ui
