//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TextView class, the primary text editing view for
// Viper's TUI. TextView displays the contents of a TextBuffer with cursor
// navigation, text selection, scrolling, optional line numbers, syntax
// highlighting, and match highlighting.
//
// TextView is a Widget subclass that binds to a TextBuffer (non-owning
// reference) and a Theme. It handles keyboard events for cursor movement
// (arrows, Home, End, Page Up/Down) and selection (Shift+arrow). The view
// maintains a viewport (top_row_) that scrolls to keep the cursor visible.
//
// Optional features:
//   - Line numbers: rendered in a left gutter when showLineNumbers is true.
//   - Syntax highlighting: applied via an attached SyntaxRuleSet pointer.
//   - Match highlighting: byte ranges set via setHighlights() are rendered
//     with the selection style.
//
// Key invariants:
//   - The cursor position (cursor_row_, cursor_col_) is always within valid
//     buffer bounds.
//   - The viewport scrolls automatically to keep the cursor visible.
//   - Selection is tracked as a byte range (sel_start_, sel_end_).
//
// Ownership: TextView borrows the TextBuffer, Theme, and optional
// SyntaxRuleSet by reference/pointer. All must outlive the TextView.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/ui/widget.hpp"
#include "tui/util/unicode.hpp"

namespace viper::tui::syntax
{
class SyntaxRuleSet;
}

namespace viper::tui::views
{

/// @brief Interactive text editing view bound to a TextBuffer.
/// @details Renders buffer content with optional line numbers, syntax
///          highlighting, and match highlighting. Handles keyboard navigation,
///          selection, and scrolling. Designed to be embedded in containers
///          and managed by the App's focus system.
class TextView : public ui::Widget
{
  public:
    /// @brief Construct a TextView bound to a text buffer and theme.
    /// @param buf Text buffer to display and edit. Must outlive the view.
    /// @param theme Theme providing color styles. Must outlive the view.
    /// @param showLineNumbers When true, render line numbers in a left gutter.
    TextView(text::TextBuffer &buf, const style::Theme &theme, bool showLineNumbers = false);

    /// @brief Paint visible lines into the screen buffer.
    /// @details Renders the viewport region of the text buffer, applying
    ///          syntax highlighting, selection highlighting, and optional
    ///          line numbers. Scrolls the viewport to keep the cursor visible.
    /// @param sb Screen buffer to paint into.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle navigation and editing key events.
    /// @details Processes arrow keys, Home/End, Page Up/Down, and their
    ///          Shift variants for selection. Also handles character insertion
    ///          and deletion keys.
    /// @param ev Input event to handle.
    /// @return True if the event was consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief TextView always wants focus for text editing.
    /// @return Always true.
    [[nodiscard]] bool wantsFocus() const override;

    /// @brief Get the current cursor row (0-based line number).
    /// @return Cursor row index.
    [[nodiscard]] std::size_t cursorRow() const;

    /// @brief Get the current cursor column in display cells (0-based).
    /// @return Cursor column index.
    [[nodiscard]] std::size_t cursorCol() const;

    /// @brief Set byte ranges to highlight (e.g., search matches).
    /// @details Each range is a (start, length) pair. Ranges are rendered
    ///          using the selection style from the theme.
    /// @param ranges Vector of (offset, length) pairs to highlight.
    void setHighlights(std::vector<std::pair<std::size_t, std::size_t>> ranges);

    /// @brief Move the cursor to a specific byte offset in the buffer.
    /// @details Updates cursor_row_ and cursor_col_ to match the given offset
    ///          and scrolls the viewport if necessary.
    /// @param off Byte offset to move the cursor to.
    void moveCursorToOffset(std::size_t off);

    /// @brief Attach a syntax rule set for source code highlighting.
    /// @details When set, syntax spans are computed per visible line during
    ///          paint(). Pass nullptr to disable syntax highlighting.
    /// @param syntax Pointer to a SyntaxRuleSet. Borrowed; must outlive the view.
    void setSyntax(syntax::SyntaxRuleSet *syntax);

  private:
    text::TextBuffer &buf_;
    const style::Theme &theme_;
    bool show_line_numbers_{};
    syntax::SyntaxRuleSet *syntax_{nullptr};

    std::size_t cursor_row_{0};
    std::size_t cursor_col_{0};
    std::size_t target_col_{0};
    std::size_t top_row_{0};

    std::size_t sel_start_{0};
    std::size_t sel_end_{0};
    std::size_t cursor_offset_{0};

    std::vector<std::pair<std::size_t, std::size_t>> highlights_{};

    // helpers
    static std::pair<char32_t, std::size_t> decodeChar(std::string_view s, std::size_t off);
    static std::size_t lineWidth(std::string_view line);
    static std::size_t columnToOffset(std::string_view line, std::size_t col);
    std::size_t offsetFromRowCol(std::size_t row, std::size_t col) const;
    std::size_t totalLines() const;
    void setCursor(std::size_t row, std::size_t col, bool shift, bool updateTarget);
};

} // namespace viper::tui::views
