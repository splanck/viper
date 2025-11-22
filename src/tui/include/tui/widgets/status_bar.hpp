//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/widgets/status_bar.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{
/// @brief Displays status information at the bottom line.
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
