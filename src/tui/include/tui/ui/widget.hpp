//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/ui/widget.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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
struct Rect
{
    int x{0};
    int y{0};
    int w{0};
    int h{0};
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
