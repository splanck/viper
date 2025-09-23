// tui/src/text/LineIndex.cpp
// @brief LineIndex implementation reacting to piece table span callbacks.
// @invariant Offsets remain sorted with sentinel zero entry.
// @ownership Stores offsets only; text lifetimes managed by callers.

#include "tui/text/LineIndex.hpp"

#include <algorithm>

namespace viper::tui::text
{
void LineIndex::reset(std::string_view text)
{
    line_starts_.clear();
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            line_starts_.push_back(i + 1);
        }
    }
}

void LineIndex::onInsert(std::size_t pos, std::string_view text)
{
    if (text.empty())
    {
        return;
    }

    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), pos);
    std::size_t idx = static_cast<std::size_t>(it - line_starts_.begin());
    for (std::size_t i = idx; i < line_starts_.size(); ++i)
    {
        line_starts_[i] += text.size();
    }

    std::size_t insert_idx = idx;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            line_starts_.insert(line_starts_.begin() + insert_idx, pos + i + 1);
            ++insert_idx;
        }
    }
}

void LineIndex::onErase(std::size_t pos, std::string_view text)
{
    if (text.empty())
    {
        return;
    }

    std::size_t len = text.size();
    auto start_it = std::lower_bound(line_starts_.begin() + 1, line_starts_.end(), pos);
    auto end_it = std::lower_bound(start_it, line_starts_.end(), pos + len);
    std::size_t start_idx = static_cast<std::size_t>(start_it - line_starts_.begin());
    std::size_t end_idx = static_cast<std::size_t>(end_it - line_starts_.begin());
    line_starts_.erase(line_starts_.begin() + start_idx, line_starts_.begin() + end_idx);
    for (std::size_t i = start_idx; i < line_starts_.size(); ++i)
    {
        line_starts_[i] -= len;
    }
}

std::size_t LineIndex::count() const
{
    return line_starts_.size();
}

std::size_t LineIndex::start(std::size_t line) const
{
    return line_starts_.at(line);
}
} // namespace viper::tui::text
