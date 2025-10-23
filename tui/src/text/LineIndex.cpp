//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/LineIndex.cpp
// Purpose: Maintain the mapping between line numbers and byte offsets for the
//          text buffer abstraction used by the editor widgets.
// Key invariants: Offsets remain sorted, include a sentinel zero entry, and are
//                 updated in response to piece table insert/erase callbacks.
// Ownership/Lifetime: The index stores offsets only; the underlying text is
//                     owned by the piece table/TextBuffer.
// Links: docs/tui/text-buffer.md#line-index
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the incremental line index used by text views.
/// @details The index listens to modifications reported by the piece table and
///          keeps a sorted vector of line start offsets so cursor logic can map
///          between (line, column) and byte positions efficiently.

#include "tui/text/LineIndex.hpp"

#include <algorithm>

namespace viper::tui::text
{
/// @brief Rebuild the line index from scratch using the supplied buffer.
/// @details Clears existing offsets, seeds the index with the implicit zero
///          entry, and scans the text for newline characters.  Each newline
///          contributes the byte position immediately following it, matching the
///          behaviour expected by cursor navigation utilities.
/// @param text Entire buffer contents to analyse.
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

/// @brief Update offsets to account for text inserted at a given position.
/// @details Adjusts subsequent line start positions by the length of the insert
///          and inserts new offsets for newline characters contained within the
///          inserted text.  The algorithm uses upper_bound to locate the first
///          line start greater than the insertion point, ensuring offsets remain
///          sorted.
/// @param pos Byte position where the new text was inserted.
/// @param text Text fragment that was inserted into the buffer.
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

/// @brief Update offsets after a contiguous span of text has been erased.
/// @details Removes any line starts that fall within the erased region and shifts
///          the remaining offsets backward by the number of erased bytes.  Using
///          lower_bound avoids linear scans when the deletion affects later
///          lines.
/// @param pos Byte position where the removal began.
/// @param text Text fragment that was removed; only its length is used.
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

/// @brief Report the number of tracked line start offsets.
/// @details Because the vector always includes a sentinel zero entry, the count
///          equals the number of logical lines plus one.  Consumers typically
///          subtract one when presenting line counts to the user.
/// @return Total number of stored offsets.
std::size_t LineIndex::count() const
{
    return line_starts_.size();
}

/// @brief Retrieve the byte offset marking the start of a given line.
/// @details Uses @ref std::vector::at to provide bounds checking in debug builds,
///          helping catch incorrect callers while keeping release builds fast.
/// @param line Zero-based line index to resolve.
/// @return Byte offset corresponding to the first character on the line.
std::size_t LineIndex::start(std::size_t line) const
{
    return line_starts_.at(line);
}
} // namespace viper::tui::text
