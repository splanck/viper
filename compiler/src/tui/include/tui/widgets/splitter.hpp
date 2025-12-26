//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/widgets/splitter.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{
/// @brief Split container dividing area into left and right parts.
class HSplitter : public ui::Widget
{
  public:
    /// @brief Construct horizontal splitter.
    /// @param left Widget placed on the left side.
    /// @param right Widget placed on the right side.
    /// @param ratio Fraction [0,1] of width for the left widget.
    HSplitter(std::unique_ptr<ui::Widget> left, std::unique_ptr<ui::Widget> right, float ratio);

    /// @brief Layout children within given rectangle using ratio.
    void layout(const ui::Rect &r) override;

    /// @brief Paint both child widgets.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle keyboard events for adjusting split ratio.
    bool onEvent(const viper::tui::term::KeyEvent &ev);

    bool onEvent(const ui::Event &ev) override;

  private:
    std::unique_ptr<ui::Widget> left_{};
    std::unique_ptr<ui::Widget> right_{};
    float ratio_{0.5F};
};

/// @brief Split container dividing area into top and bottom parts.
class VSplitter : public ui::Widget
{
  public:
    /// @brief Construct vertical splitter.
    /// @param top Widget placed at the top.
    /// @param bottom Widget placed at the bottom.
    /// @param ratio Fraction [0,1] of height for the top widget.
    VSplitter(std::unique_ptr<ui::Widget> top, std::unique_ptr<ui::Widget> bottom, float ratio);

    /// @brief Layout children within given rectangle using ratio.
    void layout(const ui::Rect &r) override;

    /// @brief Paint both child widgets.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle keyboard events for adjusting split ratio.
    bool onEvent(const viper::tui::term::KeyEvent &ev);

    bool onEvent(const ui::Event &ev) override;

  private:
    std::unique_ptr<ui::Widget> top_{};
    std::unique_ptr<ui::Widget> bottom_{};
    float ratio_{0.5F};
};

} // namespace viper::tui::widgets
