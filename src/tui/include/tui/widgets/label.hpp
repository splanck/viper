//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/widgets/label.hpp
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

/// @brief Displays read-only text.
class Label : public ui::Widget
{
  public:
    /// @brief Construct label with text and theme.
    /// @param text Text to display.
    /// @param theme Theme providing colors.
    explicit Label(std::string text, const style::Theme &theme);

    /// @brief Paint text into the screen buffer.
    void paint(render::ScreenBuffer &sb) override;

  private:
    std::string text_{};
    const style::Theme &theme_;
};

} // namespace viper::tui::widgets
