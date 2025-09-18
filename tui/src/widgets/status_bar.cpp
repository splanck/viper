// tui/src/widgets/status_bar.cpp
// @brief StatusBar widget painting text on bottom line.
// @invariant Left and right segments remain within widget width.
// @ownership StatusBar borrows Theme for styling.

#include "tui/widgets/status_bar.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{
StatusBar::StatusBar(std::string left, std::string right, const style::Theme &theme)
    : left_(std::move(left)), right_(std::move(right)), theme_(theme)
{
}

void StatusBar::setLeft(std::string left)
{
    left_ = std::move(left);
}

void StatusBar::setRight(std::string right)
{
    right_ = std::move(right);
}

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
