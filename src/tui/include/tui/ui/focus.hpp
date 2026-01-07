//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/ui/focus.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <vector>

namespace viper::tui::ui
{
class Widget;

/// @brief Tracks focusable widgets in registration order.
class FocusManager
{
  public:
    /// @brief Register a widget if it wants focus.
    void registerWidget(Widget *w);

    /// @brief Unregister a widget (safe to call during destruction).
    void unregisterWidget(Widget *w);

    /// @brief Advance to the next focusable widget.
    [[nodiscard]] Widget *next();

    /// @brief Move to the previous focusable widget.
    [[nodiscard]] Widget *prev();

    /// @brief Return currently focused widget, if any.
    [[nodiscard]] Widget *current() const;

  private:
    std::vector<Widget *> ring_{};
    std::size_t index_{0};
};

} // namespace viper::tui::ui
