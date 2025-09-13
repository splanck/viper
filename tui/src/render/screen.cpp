// tui/src/render/screen.cpp
// @brief Implementation of ScreenBuffer diffing utilities.
// @invariant prev_ snapshot matches cell dimensions when diffing.
// @ownership ScreenBuffer owns current and previous cell buffers.

#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::render
{

void ScreenBuffer::resize(int rows, int cols)
{
    rows_ = rows;
    cols_ = cols;
    cells_.resize(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    prev_.resize(cells_.size());
}

Cell &ScreenBuffer::at(int y, int x)
{
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
}

const Cell &ScreenBuffer::at(int y, int x) const
{
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
}

void ScreenBuffer::clear(const Style &style)
{
    Cell blank{};
    blank.ch = U' ';
    blank.style = style;
    blank.width = 1;
    std::fill(cells_.begin(), cells_.end(), blank);
}

void ScreenBuffer::snapshotPrev()
{
    if (prev_.size() != cells_.size())
    {
        prev_.resize(cells_.size());
    }
    std::copy(cells_.begin(), cells_.end(), prev_.begin());
}

void ScreenBuffer::computeDiff(std::vector<DiffSpan> &outSpans) const
{
    outSpans.clear();
    if (prev_.size() != cells_.size())
    {
        for (int y = 0; y < rows_; ++y)
        {
            if (cols_ > 0)
            {
                outSpans.push_back(DiffSpan{y, 0, cols_});
            }
        }
        return;
    }

    for (int y = 0; y < rows_; ++y)
    {
        int row_start = y * cols_;
        int x = 0;
        while (x < cols_)
        {
            if (cells_[row_start + x] != prev_[row_start + x])
            {
                int x0 = x;
                ++x;
                while (x < cols_ && cells_[row_start + x] != prev_[row_start + x])
                {
                    ++x;
                }
                outSpans.push_back(DiffSpan{y, x0, x});
            }
            else
            {
                ++x;
            }
        }
    }
}

} // namespace viper::tui::render
