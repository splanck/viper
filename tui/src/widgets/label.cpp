// tui/src/widgets/label.cpp
// @brief Label widget rendering plain text.
// @invariant Text length does not exceed layout width.
// @ownership Label borrows Theme reference.

#include "tui/widgets/label.hpp"
#include "tui/render/screen.hpp"

namespace viper::tui::widgets
{

Label::Label(std::string text, const style::Theme &theme) : text_(std::move(text)), theme_(theme) {}

void Label::paint(render::ScreenBuffer &sb)
{
    const auto &st = theme_.style(style::Role::Normal);
    for (std::size_t i = 0; i < text_.size() && static_cast<int>(i) < rect_.w; ++i)
    {
        auto &cell = sb.at(rect_.y, rect_.x + static_cast<int>(i));
        cell.ch = static_cast<char32_t>(text_[i]);
        cell.style = st;
    }
}

} // namespace viper::tui::widgets
