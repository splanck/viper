//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements horizontal and vertical splitter widgets for the Viper TUI. The
// widgets divide their assigned rectangle between two child widgets based on a
// persisted ratio that can be tweaked interactively with Ctrl+arrow key input.
// Each splitter honours the `ui::Widget` lifecycle by recomputing child layouts
// whenever the parent rectangle changes and by delegating painting and event
// dispatch to its children.  This translation unit focuses purely on runtime
// behaviour; view-specific styling is delegated to the nested widgets.
//
// Invariants:
//   * The stored ratio is always clamped between 5% and 95% to prevent a child
//     from collapsing entirely and becoming impossible to resize back.
//   * Layout propagation maintains the splitter's rectangle so repainting uses
//     the same geometry observed during event handling.
// Ownership:
//   * Splitter instances take ownership of their child widgets via
//     `std::unique_ptr` and destroy them alongside the splitter itself.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines horizontal and vertical splitter widget implementations.
/// @details The helpers here compute child rectangles, forward paint requests,
///          and adjust ratios when keyboard events are received.  Keeping the
///          implementation in a single unit avoids duplication between the two
///          orientations while retaining straightforward control flow.

#include "tui/widgets/splitter.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{
/// @brief Create a horizontal splitter with the given children and initial ratio.
/// @details Transfers ownership of @p left and @p right into the splitter and
///          stores the requested division ratio without clamping.  The first
///          call to @ref layout() computes the actual child rectangles based on
///          the available width.
HSplitter::HSplitter(std::unique_ptr<ui::Widget> left,
                     std::unique_ptr<ui::Widget> right,
                     float ratio)
    : left_(std::move(left)), right_(std::move(right)), ratio_(ratio)
{
}

/// @brief Distribute the assigned rectangle between the splitter children.
/// @details The helper multiplies the total width by the stored ratio, clamps
///          the result within the parent's bounds, and recomputes child
///          rectangles before forwarding the layout call to each present child.
///          Using the parent's rectangle ensures resize events keep the layout
///          consistent with the geometry used for painting.
void HSplitter::layout(const ui::Rect &r)
{
    Widget::layout(r);
    int leftW = static_cast<int>(static_cast<float>(r.w) * ratio_);
    leftW = std::clamp(leftW, 0, r.w);
    int rightW = r.w - leftW;
    ui::Rect lr{r.x, r.y, leftW, r.h};
    ui::Rect rr{r.x + leftW, r.y, rightW, r.h};
    if (left_)
    {
        left_->layout(lr);
    }
    if (right_)
    {
        right_->layout(rr);
    }
}

/// @brief Paint both child widgets into the provided screen buffer.
/// @details Delegates to the children in declaration order, allowing them to
///          render overlapping content if desired.  Splitters themselves emit no
///          pixels beyond what their children produce.
void HSplitter::paint(render::ScreenBuffer &sb)
{
    if (left_)
    {
        left_->paint(sb);
    }
    if (right_)
    {
        right_->paint(sb);
    }
}

/// @brief Construct a vertical splitter with the supplied child widgets.
/// @details Ownership is transferred to the splitter and the starting ratio is
///          stored verbatim.  Actual clamping occurs during layout to preserve
///          precision between calls.
VSplitter::VSplitter(std::unique_ptr<ui::Widget> top,
                     std::unique_ptr<ui::Widget> bottom,
                     float ratio)
    : top_(std::move(top)), bottom_(std::move(bottom)), ratio_(ratio)
{
}

/// @brief Split the rectangle into stacked child regions and propagate layout.
/// @details Calculates the pixel height for the top child from the ratio,
///          clamps it within bounds, and assigns the remainder to the bottom
///          child before invoking their respective @ref layout() methods.
void VSplitter::layout(const ui::Rect &r)
{
    Widget::layout(r);
    int topH = static_cast<int>(static_cast<float>(r.h) * ratio_);
    topH = std::clamp(topH, 0, r.h);
    int bottomH = r.h - topH;
    ui::Rect tr{r.x, r.y, r.w, topH};
    ui::Rect br{r.x, r.y + topH, r.w, bottomH};
    if (top_)
    {
        top_->layout(tr);
    }
    if (bottom_)
    {
        bottom_->layout(br);
    }
}

/// @brief Delegate painting to both vertical splitter children.
void VSplitter::paint(render::ScreenBuffer &sb)
{
    if (top_)
    {
        top_->paint(sb);
    }
    if (bottom_)
    {
        bottom_->paint(sb);
    }
}

} // namespace viper::tui::widgets

namespace viper::tui::widgets
{

/// @brief Clamp splitter ratios to a practical interactive range.
/// @details Prevents callers from collapsing a child entirely (less than 5%) or
///          hiding the opposite child (greater than 95%).  This keeps keyboard
///          adjustments responsive even after repeated changes.
/// @param r Requested ratio from user input.
/// @return Ratio snapped into the inclusive [0.05, 0.95] interval.
static inline float clamp_ratio(float r)
{
    if (r < 0.05F)
        return 0.05F;
    if (r > 0.95F)
        return 0.95F;
    return r;
}

/// @brief Handle keyboard input for resizing the horizontal splitter.
/// @details Reacts to Ctrl+Left and Ctrl+Right by nudging the ratio in 5%
///          increments, clamps the stored value, and triggers a relayout of the
///          cached rectangle.  Other events are ignored so focus traversal and
///          child handling continue to work as normal.
/// @param ev Key event describing the user input.
/// @return True when the ratio changed and a relayout occurred.
bool HSplitter::onEvent(const viper::tui::term::KeyEvent &ev)
{
    using viper::tui::term::KeyEvent;
    if ((ev.mods & KeyEvent::Ctrl) == 0)
        return false;

    bool changed = false;
    if (ev.code == KeyEvent::Code::Left)
    {
        ratio_ = clamp_ratio(ratio_ - 0.05F);
        changed = true;
    }
    if (ev.code == KeyEvent::Code::Right)
    {
        ratio_ = clamp_ratio(ratio_ + 0.05F);
        changed = true;
    }
    if (!changed)
        return false;

    layout(rect_);
    return true;
}

/// @brief Handle keyboard input for resizing the vertical splitter.
/// @details Mirrors the horizontal behaviour but maps Ctrl+Up and Ctrl+Down to
///          ratio adjustments.  Successful updates recompute child layouts so
///          painting reflects the new heights immediately.
/// @param ev Key event describing the user input.
/// @return True when the ratio changed and children were relaid out.
bool VSplitter::onEvent(const viper::tui::term::KeyEvent &ev)
{
    using viper::tui::term::KeyEvent;
    if ((ev.mods & KeyEvent::Ctrl) == 0)
        return false;

    bool changed = false;
    if (ev.code == KeyEvent::Code::Up)
    {
        ratio_ = clamp_ratio(ratio_ - 0.05F);
        changed = true;
    }
    if (ev.code == KeyEvent::Code::Down)
    {
        ratio_ = clamp_ratio(ratio_ + 0.05F);
        changed = true;
    }
    if (!changed)
        return false;

    layout(rect_);
    return true;
}

/// @brief Bridge generic UI events to the horizontal splitter key handler.
/// @details Extracts the contained key event and reuses the specialised
///          overload so mouse or focus events continue to bubble normally.
bool HSplitter::onEvent(const ui::Event &ev)
{
    return onEvent(ev.key);
}

/// @brief Bridge generic UI events to the vertical splitter key handler.
bool VSplitter::onEvent(const ui::Event &ev)
{
    return onEvent(ev.key);
}

} // namespace viper::tui::widgets
