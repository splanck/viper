// tui/include/tui/ui/focus.hpp
// @brief Manage focusable widgets and current focus.
// @invariant Holds pointers to widgets that remain valid while registered.
// @ownership FocusManager does not own widgets; widgets must unregister before destruction.
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
