//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the simplest text widget used by the terminal UI: a static label
// that renders a string using the active theme.  The code lives here instead of
// inline in the header so future enhancements (alignment, wrapping, ellipsis)
// can be added without expanding the header surface.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Plain-text label widget implementation.
/// @details Labels render their contents directly into a @ref ScreenBuffer using
///          the role provided by the bound theme.  The implementation is small
///          but documented thoroughly to serve as an example for other widgets.

#include "tui/widgets/label.hpp"
#include "tui/render/screen.hpp"

namespace viper::tui::widgets
{

/// @brief Construct a label with static text and a borrowed theme.
/// @details Stores the provided string by value and keeps a reference to the
///          theme so later paint calls can query style roles.  Labels remain
///          lightweight so they can be sprinkled liberally throughout layouts.
Label::Label(std::string text, const style::Theme &theme) : text_(std::move(text)), theme_(theme) {}

/// @brief Render the label contents into the supplied screen buffer.
/// @details Iterates over the visible characters in @ref text_, clamps drawing to
///          the widget rectangle, and writes glyph/style pairs directly into the
///          screen buffer.  Characters beyond the available width are truncated
///          so the widget never wraps implicitly.
void Label::paint(render::ScreenBuffer &sb)
{
    const auto &st = theme_.style(style::Role::Normal);
    for (std::size_t i = 0; i < text_.size() && static_cast<int>(i) < rect_.w; ++i)
    {
        auto &cell = sb.at(rect_.y, rect_.x + static_cast<int>(i));
        cell.ch = static_cast<char32_t>(text_[i]);
        cell.style = st;
    }
}

} // namespace viper::tui::widgets
