// tui/src/widgets/button.cpp
// @brief Button widget with ASCII border and activation keys.
// @invariant Border size matches layout rectangle.
// @ownership Button borrows Theme and callback.

#include "tui/widgets/button.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{

Button::Button(std::string text, OnClick onClick, const style::Theme &theme)
    : text_(std::move(text)), onClick_(std::move(onClick)), theme_(theme)
{
}

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

/// @brief Buttons request focus so they can respond to activation keys.
bool Button::wantsFocus() const
{
    return true;
}

} // namespace viper::tui::widgets
