// tui/include/tui/ui/focus.hpp
// @brief Manage a ring of focusable widgets and track focused widget.
// @invariant Stored pointers are non-null and index is -1 or a valid element.
// @ownership FocusManager does not own widgets; callers must unregister destroyed widgets.
#pragma once

#include <vector>

namespace viper::tui::ui
{
class Widget;

/// @brief Manages focus among registered widgets.
class FocusManager
{
  public:
    /// @brief Register a widget if it wants focus.
    void registerWidget(Widget *w);

    /// @brief Remove a widget from focus list.
    void unregisterWidget(Widget *w);

    /// @brief Advance focus to next widget.
    Widget *next();

    /// @brief Move focus to previous widget.
    Widget *prev();

    /// @brief Get the currently focused widget.
    [[nodiscard]] Widget *focused() const;

  private:
    std::vector<Widget *> ring_{};
    int current_{-1};
};

} // namespace viper::tui::ui
