//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/text/LineIndex.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace viper::tui::text
{
/// @brief Tracks line boundaries in a text buffer.
class LineIndex
{
  public:
    /// @brief Reset to a fresh text snapshot.
    void reset(std::string_view text);

    /// @brief Apply insertion notification at byte position.
    void onInsert(std::size_t pos, std::string_view text);

    /// @brief Apply erase notification at byte position.
    void onErase(std::size_t pos, std::string_view text);

    /// @brief Number of indexed lines.
    [[nodiscard]] std::size_t count() const;

    /// @brief Starting offset of a line.
    [[nodiscard]] std::size_t start(std::size_t line) const;

  private:
    std::vector<std::size_t> line_starts_{0};
};
} // namespace viper::tui::text
