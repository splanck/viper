// tui/include/tui/widgets/search_bar.hpp
// @brief Incremental search bar for TextView highlighting.
// @invariant Query updates trigger find results and TextView cursor moves.
// @ownership SearchBar borrows TextBuffer, TextView, and Theme.
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
/// @brief Widget accepting search text and navigating matches.
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
