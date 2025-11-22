//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/render/screen.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

namespace viper::tui::render
{
struct RGBA
{
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

bool operator==(const RGBA &a, const RGBA &b);
bool operator!=(const RGBA &a, const RGBA &b);

/// @brief Attribute flags for styled cells.
enum Attr : uint16_t
{
    AttrNone = 0,
    Bold = 1 << 0,
    Faint = 1 << 1,
    Italic = 1 << 2,
    Underline = 1 << 3,
    Blink = 1 << 4,
    Reverse = 1 << 5,
    Invisible = 1 << 6,
    Strike = 1 << 7
};

/// @brief Visual style for a cell.
struct Style
{
    RGBA fg{};
    RGBA bg{};
    uint16_t attrs{0};
};

bool operator==(const Style &a, const Style &b);
bool operator!=(const Style &a, const Style &b);

/// @brief Single character cell with style and width.
struct Cell
{
    char32_t ch{U' '};
    Style style{};
    uint8_t width{1};
};

bool operator==(const Cell &a, const Cell &b);
bool operator!=(const Cell &a, const Cell &b);

/// @brief 2D grid of styled cells with diff computation.
class ScreenBuffer
{
  public:
    /// @brief Span of changed cells within a row.
    struct DiffSpan
    {
        int row{0};
        int x0{0};
        int x1{0};
    };

    /// @brief Resize buffer to given rows and columns.
    void resize(int rows, int cols);

    /// @brief Access cell at position (y, x).
    [[nodiscard]] Cell &at(int y, int x);

    /// @brief Const access to cell at position (y, x).
    [[nodiscard]] const Cell &at(int y, int x) const;

    /// @brief Fill all cells with spaces using the given style.
    void clear(const Style &style);

    /// @brief Snapshot current buffer into previous state for diffing.
    void snapshotPrev();

    /// @brief Compute differences against previous snapshot.
    /// @param outSpans Output vector receiving change spans per row.
    void computeDiff(std::vector<DiffSpan> &outSpans) const;

  private:
    int rows_{0};
    int cols_{0};
    std::vector<Cell> cells_{};
    std::vector<Cell> prev_{};
};

} // namespace viper::tui::render
