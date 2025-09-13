// tui/include/tui/render/renderer.hpp
// @brief Renderer translating ScreenBuffer diffs into ANSI sequences.
// @invariant Maintains current style to avoid redundant SGR codes.
// @ownership Renderer holds a reference to external TermIO.
#pragma once

#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"

namespace viper::tui::render
{
class Renderer
{
  public:
    /// @brief Construct renderer writing to a TermIO.
    /// @param io Terminal I/O target.
    /// @param truecolor Enable 24-bit color SGR if true, else 256-color fallback.
    explicit Renderer(::tui::term::TermIO &io, bool truecolor = false);

    /// @brief Draw changes from the given screen buffer.
    void draw(const ScreenBuffer &sb);

    /// @brief Set terminal style.
    void setStyle(Style style);

    /// @brief Move cursor to position (y, x).
    void moveCursor(int y, int x);

  private:
    ::tui::term::TermIO &io_;
    bool truecolor_{false};
    Style curr_{};
    bool haveCurr_{false};
};

} // namespace viper::tui::render
