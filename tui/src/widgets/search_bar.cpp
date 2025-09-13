// tui/src/widgets/search_bar.cpp
// @brief SearchBar widget implementation performing incremental find.
// @invariant Maintains current query and navigates through matches.
// @ownership SearchBar borrows TextBuffer, TextView, and Theme.

#include "tui/widgets/search_bar.hpp"

namespace viper::tui::widgets
{
using viper::tui::term::KeyEvent;

SearchBar::SearchBar(text::TextBuffer &buf, views::TextView &view, const style::Theme &theme)
    : buf_(buf), view_(view), theme_(theme)
{
}

void SearchBar::setRegex(bool regex)
{
    regex_ = regex;
    updateMatches();
}

void SearchBar::updateMatches()
{
    matches_ = text::findAll(buf_, query_, regex_);
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.reserve(matches_.size());
    for (const auto &m : matches_)
    {
        ranges.emplace_back(m.start, m.length);
    }
    view_.setHighlights(std::move(ranges));
    current_ = 0;
    if (!matches_.empty())
    {
        gotoMatch(0);
    }
}

void SearchBar::gotoMatch(std::size_t idx)
{
    if (idx >= matches_.size())
    {
        return;
    }
    view_.moveCursorToOffset(matches_[idx].start);
}

bool SearchBar::onEvent(const ui::Event &ev)
{
    using Code = KeyEvent::Code;
    if (ev.key.code == Code::Backspace)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            updateMatches();
        }
        return true;
    }
    if (ev.key.code == Code::Enter || ev.key.code == Code::F3)
    {
        if (!matches_.empty())
        {
            current_ = (current_ + 1) % matches_.size();
            gotoMatch(current_);
        }
        return true;
    }
    if (ev.key.code == Code::Unknown && ev.key.codepoint >= 32 && ev.key.codepoint <= 126)
    {
        query_.push_back(static_cast<char>(ev.key.codepoint));
        updateMatches();
        return true;
    }
    return false;
}

void SearchBar::paint(render::ScreenBuffer &sb)
{
    const auto &st = theme_.style(style::Role::Normal);
    std::string text = "/" + query_;
    for (int i = 0; i < rect_.w; ++i)
    {
        auto &cell = sb.at(rect_.y, rect_.x + i);
        if (i < static_cast<int>(text.size()))
        {
            cell.ch = static_cast<char32_t>(text[i]);
        }
        else
        {
            cell.ch = U' ';
        }
        cell.style = st;
    }
}

} // namespace viper::tui::widgets
