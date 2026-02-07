//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Button widget for Viper's TUI framework.
// Button displays a labeled interactive element with a border that can
// be activated by pressing Enter when focused.
//
// The button renders its label text centered within a bordered rectangle
// using theme-appropriate styles. When the user presses Enter while the
// button has focus, the registered onClick callback is invoked.
//
// Key invariants:
//   - The button is always focusable (wantsFocus returns true).
//   - The onClick callback may be empty (activation is a no-op).
//   - The border consumes 1 cell on each side of the button area.
//
// Ownership: Button owns its label string and callback by value.
// The Theme reference is borrowed (must outlive the widget).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Interactive button widget with border, label, and activation callback.
/// @details Renders a bordered rectangle with centered label text. When focused,
///          responds to Enter/Return key presses by invoking the onClick callback.
///          Styled using the theme's normal and accent roles for unfocused and
///          focused states respectively.
class Button : public ui::Widget
{
  public:
    using OnClick = std::function<void()>;

    /// @brief Construct button.
    /// @param text Button label text.
    /// @param onClick Callback invoked on activation.
    /// @param theme Theme providing styles.
    Button(std::string text, OnClick onClick, const style::Theme &theme);

    /// @brief Paint button with border and label.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle key events for activation.
    /// @return True if event consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief Buttons want focus to receive input.
    [[nodiscard]] bool wantsFocus() const override;

  private:
    std::string text_{};
    OnClick onClick_{};
    const style::Theme &theme_;
};

} // namespace viper::tui::widgets
