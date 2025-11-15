// tui/include/tui/views/text_view.hpp
// @brief Scrollable text view with cursor and selection.
// @invariant Cursor stays within buffer bounds; selection uses byte offsets.
// @ownership TextView borrows TextBuffer and Theme.
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

/// @brief View for editing a TextBuffer with cursor and optional selection.
class TextView : public ui::Widget
{
  public:
    /// @brief Construct a TextView bound to a buffer.
    /// @param buf Text buffer to view/edit.
    /// @param theme Theme providing styles.
    /// @param showLineNumbers Whether to render line numbers.
    TextView(text::TextBuffer &buf, const style::Theme &theme, bool showLineNumbers = false);

    /// @brief Paint visible lines into the screen buffer.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle navigation keys for cursor and selection.
    /// @return True if the event was consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief TextView wants focus for editing.
    [[nodiscard]] bool wantsFocus() const override;

    /// @brief Current cursor row (0-based).
    [[nodiscard]] std::size_t cursorRow() const;

    /// @brief Current cursor column in display cells (0-based).
    [[nodiscard]] std::size_t cursorCol() const;

    /// @brief Set byte ranges to highlight.
    void setHighlights(std::vector<std::pair<std::size_t, std::size_t>> ranges);

    /// @brief Move cursor to byte offset.
    void moveCursorToOffset(std::size_t off);

    /// @brief Attach a syntax rule set for highlighting.
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
