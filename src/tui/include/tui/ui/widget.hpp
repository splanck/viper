//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Widget base class and the Rect layout primitive for
// Viper's TUI framework. Widget is the abstract base for all visual elements
// in the widget tree, defining the interface for layout, painting, input
// handling, and focus participation.
//
// The widget lifecycle follows a layout-then-paint pattern: the parent calls
// layout() to assign the widget's rectangle, then paint() to render content
// into a screen buffer. Input events arrive via onEvent() when the widget
// has focus, and wantsFocus() controls whether the widget participates in
// the focus ring.
//
// Key invariants:
//   - layout() must be called before paint() for correct rendering.
//   - The rect_ member reflects the last layout() call.
//   - Subclasses should call Widget::layout() to store the rectangle.
//   - wantsFocus() defaults to false; focusable widgets must override.
//
// Ownership: Widget is a polymorphic base class. Containers own their
// children via unique_ptr. Widget itself holds no heap resources.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/ui/event.hpp"

namespace viper::tui::render
{
class ScreenBuffer;
}

namespace viper::tui::ui
{
/// @brief Axis-aligned rectangle used for widget layout and hit testing.
/// @details Stores position (x, y) and dimensions (w, h) in terminal cell
///          coordinates. Used by the layout system to assign screen regions
///          to widgets.
struct Rect
{
    int x{0};
    int y{0};
    int w{0};
    int h{0};
};

/// @brief Abstract base class for all visual elements in the TUI widget tree.
/// @details Defines the core interface for layout computation, screen painting,
///          input event handling, and focus management. Subclasses implement
///          specific visual behaviors (buttons, text views, containers, etc.).
///          Widgets are organized in a tree hierarchy and rendered in depth-first
///          order by the App's render loop.
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

    /// @brief Whether this widget can receive focus.
    [[nodiscard]] virtual bool wantsFocus() const;

    /// @brief Notifies widget when it gains or loses input focus.
    /// Default implementation is a no-op.
    virtual void onFocusChanged(bool focused);

    /// @brief Retrieve widget rectangle.
    [[nodiscard]] Rect rect() const;

  protected:
    Rect rect_{};
};

} // namespace viper::tui::ui
