// tui/include/tui/widgets/label.hpp
// @brief Simple text label widget.
// @invariant Stores immutable text and theme reference.
// @ownership Label does not own the Theme.
#pragma once

#include <string>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Displays read-only text.
class Label : public ui::Widget
{
  public:
    /// @brief Construct label with text and theme.
    /// @param text Text to display.
    /// @param theme Theme providing colors.
    explicit Label(std::string text, const style::Theme &theme);

    /// @brief Paint text into the screen buffer.
    void paint(render::ScreenBuffer &sb) override;

  private:
    std::string text_{};
    const style::Theme &theme_;
};

} // namespace viper::tui::widgets
