// tui/include/tui/views/text_view.hpp
// @brief Scrollable text view with cursor and selection.
// @invariant Cursor stays within buffer bounds; selection uses byte offsets.
// @ownership TextView borrows TextBuffer and Theme.
#pragma once

#include <cstddef>

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/ui/widget.hpp"
#include "tui/util/unicode.hpp"

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
    [[nodiscard]] bool wantsFocus() const override
    {
        return true;
    }

    /// @brief Current cursor row (0-based).
    [[nodiscard]] std::size_t cursorRow() const
    {
        return cursor_row_;
    }

    /// @brief Current cursor column in display cells (0-based).
    [[nodiscard]] std::size_t cursorCol() const
    {
        return cursor_col_;
    }

  private:
    text::TextBuffer &buf_;
    const style::Theme &theme_;
    bool show_line_numbers_{};

    std::size_t cursor_row_{0};
    std::size_t cursor_col_{0};
    std::size_t target_col_{0};
    std::size_t top_row_{0};

    std::size_t sel_start_{0};
    std::size_t sel_end_{0};
    std::size_t cursor_offset_{0};

    // helpers
    static std::pair<char32_t, std::size_t> decodeChar(const std::string &s, std::size_t off);
    static std::size_t lineWidth(const std::string &line);
    static std::size_t columnToOffset(const std::string &line, std::size_t col);
    std::size_t offsetFromRowCol(std::size_t row, std::size_t col) const;
    std::size_t totalLines() const;
    void setCursor(std::size_t row, std::size_t col, bool shift, bool updateTarget);
};

} // namespace viper::tui::views
