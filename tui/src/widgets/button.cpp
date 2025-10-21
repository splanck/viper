//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// tui/src/widgets/button.cpp
//
// Implements a basic push button widget for the terminal UI toolkit.  The
// widget renders a bordered rectangle, centres its label text, and invokes a
// caller-supplied callback when activated via keyboard.  It relies on the
// global theme palette to colour the border and text, keeping visuals consistent
// across the application without embedding styling decisions here.
//
//===----------------------------------------------------------------------===//

#include "tui/widgets/button.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{
/// @brief Construct a button with label text, callback, and theme reference.
///
/// @details The label is stored by value while the click handler and theme are
///          kept by reference, allowing the widget to respond to activations
///          without owning additional resources.  Callbacks can be empty, in
///          which case activation simply performs no action.
Button::Button(std::string text, OnClick onClick, const style::Theme &theme)
    : text_(std::move(text)), onClick_(std::move(onClick)), theme_(theme)
{
}

/// @brief Paint the button's border, fill, and label text into the screen buffer.
///
/// @details The routine first queries the theme for accent and normal styles,
///          then draws a rectangular border using ASCII characters.  Interior
///          cells are cleared to spaces with the normal style applied.  When the
///          height allows, the label text is centred vertically and truncated to
///          fit horizontally.  All drawing respects the widget's layout
///          rectangle, ensuring compatibility with container-managed geometry.
void Button::paint(render::ScreenBuffer &sb)
{
    const auto &border = theme_.style(style::Role::Accent);
    const auto &txt = theme_.style(style::Role::Normal);

    int x0 = rect_.x;
    int y0 = rect_.y;
    int w = rect_.w;
    int h = rect_.h;

    // Top and bottom borders
    for (int x = 0; x < w; ++x)
    {
        auto &top = sb.at(y0, x0 + x);
        top.ch = (x == 0 || x == w - 1) ? U'+' : U'-';
        top.style = border;
        auto &bot = sb.at(y0 + h - 1, x0 + x);
        bot.ch = (x == 0 || x == w - 1) ? U'+' : U'-';
        bot.style = border;
    }

    // Sides and fill
    for (int y = 1; y < h - 1; ++y)
    {
        auto &left = sb.at(y0 + y, x0);
        left.ch = U'|';
        left.style = border;
        auto &right = sb.at(y0 + y, x0 + w - 1);
        right.ch = U'|';
        right.style = border;
        for (int x = 1; x < w - 1; ++x)
        {
            auto &cell = sb.at(y0 + y, x0 + x);
            cell.ch = U' ';
            cell.style = txt;
        }
    }

    // Minimum height of 3 required to render text inside the border.
    if (h >= 3)
    {
        // Text centered vertically while staying inside the border.
        int row = std::clamp(y0 + h / 2, y0 + 1, y0 + h - 2);
        int start = x0 + 1;
        for (std::size_t i = 0; i < text_.size() && start + static_cast<int>(i) < x0 + w - 1; ++i)
        {
            auto &cell = sb.at(row, start + static_cast<int>(i));
            cell.ch = static_cast<char32_t>(text_[i]);
            cell.style = txt;
        }
    }
}

/// @brief Handle key events that should trigger the button's onClick callback.
///
/// @details The widget reacts to Enter and Space activations.  When a callback
///          is registered it is invoked immediately, and the event is reported as
///          handled.  Other keys fall through so the event system can continue
///          propagation to other widgets if needed.
bool Button::onEvent(const ui::Event &ev)
{
    const auto &k = ev.key;
    if (k.code == term::KeyEvent::Code::Enter || k.codepoint == U' ')
    {
        if (onClick_)
        {
            onClick_();
        }
        return true;
    }
    return false;
}

/// @brief Request focus participation so activation keys reach the widget.
///
/// @details Buttons need focus to receive keyboard events, so the method
///          returns @c true.  Containers consult this when building traversal
///          order, ensuring that interactive controls behave as expected.
bool Button::wantsFocus() const
{
    return true;
}

} // namespace viper::tui::widgets
