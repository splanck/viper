// tui/include/tui/widgets/button.hpp
// @brief Clickable button widget with text.
// @invariant Callback executed on activation keys.
// @ownership Button borrows Theme and stores callback.
#pragma once

#include <functional>
#include <string>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Simple button with border and onClick handler.
class Button : public ui::Widget
{
  public:
    using OnClick = std::function<void()>;

    /// @brief Construct button.
    /// @param text Button label text.
    /// @param onClick Callback invoked on activation.
    /// @param theme Theme providing styles.
    Button(std::string text, OnClick onClick, const style::Theme &theme);

    /// @brief Paint button with border and label.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle key events for activation.
    /// @return True if event consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief Buttons want focus to receive input.
    [[nodiscard]] bool wantsFocus() const override;

  private:
    std::string text_{};
    OnClick onClick_{};
    const style::Theme &theme_;
};

} // namespace viper::tui::widgets
