// tui/src/widgets/list_view.cpp
// @brief Implementation of selectable ListView widget.
// @invariant Selection flags align with items vector.
// @ownership ListView borrows Theme; items owned by caller.

#include "tui/widgets/list_view.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{

ListView::ListView(std::vector<std::string> items, const style::Theme &theme)
    : items_(std::move(items)), theme_(theme)
{
    selected_.resize(items_.size(), false);
    if (!items_.empty())
    {
        selected_[0] = true;
    }
    renderer_ = [this](render::ScreenBuffer &sb,
                       int row,
                       const std::string &item,
                       bool selected,
                       const style::Theme &) { defaultRender(sb, row, item, selected); };
}

void ListView::setRenderer(ItemRenderer r)
{
    renderer_ = std::move(r);
}

void ListView::paint(render::ScreenBuffer &sb)
{
    for (std::size_t i = 0; i < items_.size() && static_cast<int>(i) < rect_.h; ++i)
    {
        renderer_(sb, rect_.y + static_cast<int>(i), items_[i], selected_[i], theme_);
    }
}

bool ListView::onEvent(const ui::Event &ev)
{
    if (items_.empty())
    {
        return false;
    }
    const auto &k = ev.key;
    if (k.code == term::KeyEvent::Code::Up)
    {
        if (cursor_ > 0)
        {
            --cursor_;
        }
    }
    else if (k.code == term::KeyEvent::Code::Down)
    {
        if (cursor_ + 1 < static_cast<int>(items_.size()))
        {
            ++cursor_;
        }
    }
    else
    {
        return false;
    }

    if (k.mods & term::KeyEvent::Mods::Ctrl)
    {
        return true;
    }
    if (k.mods & term::KeyEvent::Mods::Shift)
    {
        clearSelection();
        selectRange(anchor_, cursor_);
        return true;
    }
    clearSelection();
    anchor_ = cursor_;
    selected_[cursor_] = true;
    return true;
}

/// @brief List views require focus to process navigation keystrokes.
bool ListView::wantsFocus() const
{
    return true;
}

std::vector<int> ListView::selection() const
{
    std::vector<int> out;
    for (std::size_t i = 0; i < selected_.size(); ++i)
    {
        if (selected_[i])
        {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

void ListView::defaultRender(render::ScreenBuffer &sb,
                             int row,
                             const std::string &item,
                             bool selected)
{
    const auto &sel = theme_.style(style::Role::Accent);
    const auto &nor = theme_.style(style::Role::Normal);
    int x = rect_.x;
    auto &pref = sb.at(row, x);
    pref.ch = selected ? U'>' : U' ';
    pref.style = sel;
    int maxw = rect_.w - 1;
    for (int i = 0; i < maxw; ++i)
    {
        auto &c = sb.at(row, x + 1 + i);
        if (i < static_cast<int>(item.size()))
        {
            c.ch = static_cast<char32_t>(item[i]);
        }
        else
        {
            c.ch = U' ';
        }
        c.style = nor;
    }
}

void ListView::clearSelection()
{
    std::fill(selected_.begin(), selected_.end(), false);
}

void ListView::selectRange(int a, int b)
{
    if (a > b)
    {
        std::swap(a, b);
    }
    for (int i = a; i <= b && i < static_cast<int>(selected_.size()); ++i)
    {
        if (i >= 0)
        {
            selected_[i] = true;
        }
    }
}

} // namespace viper::tui::widgets
