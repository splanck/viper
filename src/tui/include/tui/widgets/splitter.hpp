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

namespace detail
{
/// @brief Clamp splitter ratios to a practical interactive range.
/// @details Prevents callers from collapsing a child entirely (less than 5%) or
///          hiding the opposite child (greater than 95%).
/// @param r Requested ratio from user input.
/// @return Ratio snapped into the inclusive [0.05, 0.95] interval.
inline float clampRatio(float r)
{
    if (r < 0.05F)
        return 0.05F;
    if (r > 0.95F)
        return 0.95F;
    return r;
}
} // namespace detail

/// @brief Base template for splitter widgets that share common logic.
/// @tparam Derived CRTP derived class type for static polymorphism.
/// @details Provides shared ratio management, child painting, and event bridging.
///          Derived classes implement axis-specific layout and key handling.
template <typename Derived> class SplitterBase : public ui::Widget
{
  public:
    /// @brief Paint both child widgets into the provided screen buffer.
    void paint(render::ScreenBuffer &sb) override
    {
        auto &self = static_cast<Derived &>(*this);
        if (self.first_)
            self.first_->paint(sb);
        if (self.second_)
            self.second_->paint(sb);
    }

    /// @brief Bridge generic UI events to the derived class key handler.
    bool onEvent(const ui::Event &ev) override
    {
        return static_cast<Derived &>(*this).onKeyEvent(ev.key);
    }

  protected:
    float ratio_{0.5F};
};

/// @brief Split container dividing area into left and right parts.
class HSplitter : public SplitterBase<HSplitter>
{
  public:
    /// @brief Construct horizontal splitter.
    /// @param left Widget placed on the left side.
    /// @param right Widget placed on the right side.
    /// @param ratio Fraction [0,1] of width for the left widget.
    HSplitter(std::unique_ptr<ui::Widget> left, std::unique_ptr<ui::Widget> right, float ratio);

    /// @brief Layout children within given rectangle using ratio.
    void layout(const ui::Rect &r) override;

    /// @brief Handle keyboard events for adjusting split ratio.
    bool onKeyEvent(const viper::tui::term::KeyEvent &ev);

  private:
    friend class SplitterBase<HSplitter>;
    std::unique_ptr<ui::Widget> first_{};  // left
    std::unique_ptr<ui::Widget> second_{}; // right
};

/// @brief Split container dividing area into top and bottom parts.
class VSplitter : public SplitterBase<VSplitter>
{
  public:
    /// @brief Construct vertical splitter.
    /// @param top Widget placed at the top.
    /// @param bottom Widget placed at the bottom.
    /// @param ratio Fraction [0,1] of height for the top widget.
    VSplitter(std::unique_ptr<ui::Widget> top, std::unique_ptr<ui::Widget> bottom, float ratio);

    /// @brief Layout children within given rectangle using ratio.
    void layout(const ui::Rect &r) override;

    /// @brief Handle keyboard events for adjusting split ratio.
    bool onKeyEvent(const viper::tui::term::KeyEvent &ev);

  private:
    friend class SplitterBase<VSplitter>;
    std::unique_ptr<ui::Widget> first_{};  // top
    std::unique_ptr<ui::Widget> second_{}; // bottom
};

} // namespace viper::tui::widgets
