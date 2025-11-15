// tui/include/tui/text/LineIndex.hpp
// @brief Maintains line start offsets reacting to span change notifications.
// @invariant line_starts_ always begins with zero and remains sorted ascending.
// @ownership LineIndex owns offset vector; callers retain referenced text buffers.
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
