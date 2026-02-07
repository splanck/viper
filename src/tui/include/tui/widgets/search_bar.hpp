//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SearchBar widget for Viper's TUI framework.
// The search bar provides an interactive text search interface bound to
// a TextBuffer and TextView, displaying a query input prefixed with '/'
// and navigating through matches as the user types.
//
// As the user types, matches are incrementally computed via the text
// search utilities (findAll). The matched ranges are highlighted in the
// associated TextView. Pressing Enter advances to the next match.
//
// Key invariants:
//   - The search bar borrows TextBuffer, TextView, and Theme references.
//   - Match highlighting is updated on every keystroke via setHighlights().
//   - Empty queries clear all highlights.
//   - Regex mode can be toggled via setRegex().
//
// Ownership: SearchBar borrows all external references (TextBuffer,
// TextView, Theme). It owns the query string and match results.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "tui/style/theme.hpp"
#include "tui/text/search.hpp"
#include "tui/text/text_buffer.hpp"
#include "tui/ui/widget.hpp"
#include "tui/views/text_view.hpp"

namespace viper::tui::widgets
{
/// @brief Interactive text search widget with incremental match highlighting.
/// @details Provides a '/' prefixed query input that searches through a TextBuffer
///          and highlights matches in the associated TextView. Supports both literal
///          substring and regex search modes. Pressing Enter navigates to the next match.
class SearchBar : public ui::Widget
{
  public:
    /// @brief Construct search bar bound to buffer and view.
    SearchBar(text::TextBuffer &buf, views::TextView &view, const style::Theme &theme);

    /// @brief Paint search text prefixed by '/'.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle typing, backspace, and Enter for next match.
    bool onEvent(const ui::Event &ev) override;

    /// @brief Enable regex search mode.
    void setRegex(bool regex);

    /// @brief Number of current matches.
    [[nodiscard]] std::size_t matchCount() const;

  private:
    text::TextBuffer &buf_;
    views::TextView &view_;
    const style::Theme &theme_;
    std::string query_{};
    bool regex_{};
    std::vector<text::Match> matches_{};
    std::size_t current_{0};

    void updateMatches();
    void gotoMatch(std::size_t idx);
};
} // namespace viper::tui::widgets
