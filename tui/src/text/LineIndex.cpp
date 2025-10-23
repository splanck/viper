//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/LineIndex.cpp
// Purpose: Maintain a mapping from line numbers to byte offsets for text
//          buffers backed by a piece table.
// Key invariants: The offset list always remains sorted and includes the zero
//                 sentinel entry representing the start of the document.
// Ownership/Lifetime: The index stores offsets only; underlying text storage is
//                     owned elsewhere by the piece table.
// Links: docs/tools.md#text-buffer
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements @ref viper::tui::text::LineIndex bookkeeping operations.
/// @details The index listens to insert and erase callbacks from the piece table
///          and updates cached line start offsets so that views can resolve
///          ``line → byte`` queries in O(1) time.

#include "tui/text/LineIndex.hpp"

#include <algorithm>

namespace viper::tui::text
{
/// @brief Rebuild the line index from the provided text snapshot.
/// @details Clears any existing offsets, reinstates the leading zero entry, and
///          scans for newline characters to populate subsequent line starts.
/// @param text Buffer contents used to initialise the index.
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

/// @brief Update offsets after an insertion at @p pos.
/// @details Offsets beyond the insertion point are shifted by the inserted
///          length, while newline characters in the inserted text create new
///          line boundaries at the appropriate offsets.
/// @param pos Byte offset where new text was inserted.
/// @param text Inserted contents.
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

/// @brief Adjust offsets to account for text removal.
/// @details Removes any line starts that fall within the erased range and shifts
///          subsequent offsets backwards by the erased length.
/// @param pos Starting byte offset of the deletion.
/// @param text Text fragment that was removed (used solely for length).
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

/// @brief Query how many line starts are tracked.
/// @details The count corresponds to the number of lines in the buffer plus the
///          sentinel entry.
/// @return Number of stored offsets.
std::size_t LineIndex::count() const
{
    return line_starts_.size();
}

/// @brief Fetch the byte offset where a line begins.
/// @param line Zero-based line index.
/// @return Byte offset for @p line; throws if the index is out of range.
std::size_t LineIndex::start(std::size_t line) const
{
    return line_starts_.at(line);
}
} // namespace viper::tui::text
