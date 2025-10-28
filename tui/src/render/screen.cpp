//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the ScreenBuffer diffing utilities used by the terminal user
// interface.  The buffer tracks both the current and previous frames so the
// renderer can compute minimal spans that require repainting.  Callers interact
// with the buffer by writing cells and asking for the delta, keeping terminal
// updates efficient even on slow transports.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the ScreenBuffer double-buffering helpers.
/// @details The implementation stores two flat arrays of Cell instances: one
///          representing the frame under construction and another capturing the
///          last committed frame.  High-level widgets update the current buffer
///          and then request a diff, which yields spans describing the regions
///          that changed since the previous snapshot.

#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::render
{
/// @brief Compare two RGBA colours for equality.
/// @details Performs a field-by-field comparison of the four colour channels.
///          The helper enables direct equality checks in algorithms without
///          forcing callers to write repetitive code at the call site.
bool operator==(const RGBA &a, const RGBA &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/// @brief Determine whether two RGBA colours differ.
/// @details Inverts @ref operator== so callers can reuse the optimised
///          comparison logic and keep colour inequality checks consistent.
bool operator!=(const RGBA &a, const RGBA &b)
{
    return !(a == b);
}

/// @brief Compare two style descriptors, including colour and attributes.
/// @details Style equality requires matching foreground, background, and all
///          attribute flags.  The overload is primarily used when diffing cell
///          contents to determine whether the renderer must emit attribute
///          changes.
bool operator==(const Style &a, const Style &b)
{
    return a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs;
}

/// @brief Negate the result of Style equality.
/// @details Provides a convenient wrapper for inequality checks so calling
///          sites can remain expressive without duplicating comparisons.
bool operator!=(const Style &a, const Style &b)
{
    return !(a == b);
}

/// @brief Compare two screen cells for equality.
/// @details Cells are considered equal when the stored glyph, glyph width, and
///          visual style all match.  This definition mirrors what the renderer
///          would push to the terminal, making it a natural fit for diffing.
bool operator==(const Cell &a, const Cell &b)
{
    return a.ch == b.ch && a.width == b.width && a.style == b.style;
}

/// @brief Determine whether two screen cells differ.
/// @details Delegates to @ref operator== to ensure both overloads stay
///          perfectly in sync.
bool operator!=(const Cell &a, const Cell &b)
{
    return !(a == b);
}

/// @brief Resize the buffer to fit a terminal with @p rows and @p cols.
/// @details Updates the dimensions, resizes the active cell storage, and keeps
///          the previous snapshot in lockstep.  The function preserves existing
///          cell contents when growing so widgets can continue painting into the
///          expanded region before the next clear.
void ScreenBuffer::resize(int rows, int cols)
{
    rows_ = rows;
    cols_ = cols;
    cells_.resize(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    prev_.resize(cells_.size());
}

/// @brief Access the mutable cell located at (@p y, @p x).
/// @details Computes the flat index into the backing array.  The method trusts
///          callers to respect bounds because widgets already clamp coordinates
///          during layout.
Cell &ScreenBuffer::at(int y, int x)
{
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
}

/// @brief Access the immutable cell located at (@p y, @p x).
/// @details Shares the indexing logic with the mutable overload while exposing
///          a const reference so read-only code can inspect cell contents
///          without copying.
const Cell &ScreenBuffer::at(int y, int x) const
{
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
}

/// @brief Fill the buffer with spaces using the provided @p style.
/// @details Constructs a prototype blank cell and replicates it across the
///          entire backing store.  The width defaults to one so later diffing
///          and rendering logic maintain correct cell counts for narrow glyphs.
void ScreenBuffer::clear(const Style &style)
{
    Cell blank{};
    blank.ch = U' ';
    blank.style = style;
    blank.width = 1;
    std::fill(cells_.begin(), cells_.end(), blank);
}

/// @brief Save the current frame as the baseline for the next diff.
/// @details Resizes the snapshot storage on demand and copies the full cell
///          array.  Callers typically invoke this after flushing terminal
///          output so that subsequent calls to @ref computeDiff report only new
///          changes.
void ScreenBuffer::snapshotPrev()
{
    if (prev_.size() != cells_.size())
    {
        prev_.resize(cells_.size());
    }
    std::copy(cells_.begin(), cells_.end(), prev_.begin());
}

/// @brief Produce spans describing which regions changed since the last snapshot.
/// @details Clears the output vector and compares the active buffer with the
///          stored snapshot.  If the snapshot does not match in size—typically
///          after a resize—the method emits full-width spans for each row so the
///          caller repaints the entire screen.  Otherwise it walks each row,
///          coalescing contiguous runs of mismatched cells into `DiffSpan`
///          entries.  The result contains the minimal set of horizontal ranges
///          that require redrawing.
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
