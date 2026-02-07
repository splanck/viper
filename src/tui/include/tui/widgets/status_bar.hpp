//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the StatusBar widget for Viper's TUI framework.
// The status bar displays a single-line informational strip, typically
// positioned at the bottom of the screen, with left-aligned and
// right-aligned text segments.
//
// Common uses include showing the current file name on the left and
// cursor position or mode indicator on the right. The bar fills its
// entire allocated width with the theme's accent style.
//
// Key invariants:
//   - The bar occupies exactly one row of its layout rectangle.
//   - Left and right text are truncated if they exceed available width.
//   - The bar does not accept focus (wantsFocus returns false by default).
//
// Ownership: StatusBar owns its text strings by value and borrows
// the Theme reference (must outlive the widget).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{
/// @brief Single-line status display widget with left and right text segments.
/// @details Renders a horizontal bar spanning the full width of its layout rectangle,
///          displaying left-aligned text on the left and right-aligned text on the right.
///          Styled using the theme's accent role. Typically placed at the bottom of the
///          screen to show file information, cursor position, or mode indicators.
class StatusBar : public ui::Widget
{
  public:
    /// @brief Construct status bar with initial texts.
    /// @param left Text shown on the left side.
    /// @param right Text shown on the right side.
    /// @param theme Theme providing colors.
    StatusBar(std::string left, std::string right, const style::Theme &theme);

    /// @brief Set text on the left segment.
    void setLeft(std::string left);
    /// @brief Set text on the right segment.
    void setRight(std::string right);

    /// @brief Paint status bar into screen buffer.
    void paint(render::ScreenBuffer &sb) override;

  private:
    std::string left_{};
    std::string right_{};
    const style::Theme &theme_;
};

} // namespace viper::tui::widgets
