//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the status bar widget responsible for rendering left- and
// right-aligned messages on the bottom line of the terminal UI.  The widget is
// intentionally lightweight so it can be reused by different application modes
// without incurring extra allocations on each frame.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the status bar widget used in the Viper TUI.
/// @details The widget paints a horizontal strip using the active theme and
///          displays independent left and right strings.  It clears the row
///          before drawing text so previous frames do not leak visual artefacts.

#include "tui/widgets/status_bar.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{
/// @brief Construct a status bar with initial left and right text segments.
/// @details Stores references to the immutable theme while taking ownership of
///          the text strings.  The constructor does not perform any rendering;
///          paint operations occur later via @ref paint.
StatusBar::StatusBar(std::string left, std::string right, const style::Theme &theme)
    : left_(std::move(left)), right_(std::move(right)), theme_(theme)
{
}

/// @brief Replace the left-hand message displayed by the status bar.
/// @details The string is copied into the widget's storage so the caller may
///          discard or reuse the argument immediately.
void StatusBar::setLeft(std::string left)
{
    left_ = std::move(left);
}

/// @brief Replace the right-hand message displayed by the status bar.
/// @details Mirrors @ref setLeft while targeting the right-aligned segment.
void StatusBar::setRight(std::string right)
{
    right_ = std::move(right);
}

/// @brief Paint the status bar into the supplied screen buffer.
/// @details Clears the target row, writes the left string starting at the
///          widget's origin, and paints the right string aligned to the widget's
///          far edge.  Strings longer than the available space are clipped to
///          avoid wrapping artefacts.
void StatusBar::paint(render::ScreenBuffer &sb)
{
    const auto &st = theme_.style(style::Role::Normal);
    int y = rect_.y + rect_.h - 1;
    for (int x = 0; x < rect_.w; ++x)
    {
        auto &cell = sb.at(y, rect_.x + x);
        cell.ch = U' ';
        cell.style = st;
    }
    for (std::size_t i = 0; i < left_.size() && static_cast<int>(i) < rect_.w; ++i)
    {
        auto &cell = sb.at(y, rect_.x + static_cast<int>(i));
        cell.ch = static_cast<char32_t>(left_[i]);
        cell.style = st;
    }
    int start = rect_.x + rect_.w - static_cast<int>(right_.size());
    start = std::max(start, rect_.x);
    for (std::size_t i = 0; i < right_.size() && start + static_cast<int>(i) < rect_.x + rect_.w;
         ++i)
    {
        auto &cell = sb.at(y, start + static_cast<int>(i));
        cell.ch = static_cast<char32_t>(right_[i]);
        cell.style = st;
    }
}

} // namespace viper::tui::widgets
