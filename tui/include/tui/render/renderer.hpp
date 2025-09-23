// tui/include/tui/render/renderer.hpp
// @brief ANSI renderer emitting minimal sequences to a TermIO.
// @invariant Maintains cursor position and current style to avoid redundant sequences.
// @ownership Renderer borrows the TermIO reference; caller retains ownership.
#pragma once

#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"

namespace viper::tui::render
{

/// @brief Writes ScreenBuffer diffs as ANSI sequences to a terminal.
class Renderer
{
  public:
    /// @brief Construct renderer targeting a TermIO.
    /// @param tio Terminal I/O sink used for writes and flushes.
    /// @param truecolor Emit 24-bit color codes when true; otherwise 256-color.
    explicit Renderer(::viper::tui::term::TermIO &tio, bool truecolor = false);

    /// @brief Draw changed spans from screen buffer to terminal.
    /// @param sb Screen buffer with current and previous state for diffing.
    void draw(const ScreenBuffer &sb);

    /// @brief Update terminal style if different from current style.
    /// @param style Desired style to apply.
    void setStyle(Style style);

    /// @brief Move cursor to given coordinates if not already there.
    /// @param y Row index starting at 0.
    /// @param x Column index starting at 0.
    void moveCursor(int y, int x);

  private:
    ::viper::tui::term::TermIO &tio_;
    Style currentStyle_{};
    int cursorY_{-1};
    int cursorX_{-1};
    bool truecolor_{false};
};

} // namespace viper::tui::render
