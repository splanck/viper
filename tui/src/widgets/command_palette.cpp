// tui/src/widgets/command_palette.cpp
// @brief CommandPalette implementation filtering commands and executing.
// @invariant Query changes recompute filtered list.
// @ownership CommandPalette borrows Keymap and Theme.

#include "tui/widgets/command_palette.hpp"
#include "tui/render/screen.hpp"

namespace viper::tui::widgets
{
using viper::tui::term::KeyEvent;

CommandPalette::CommandPalette(input::Keymap &km, const style::Theme &theme)
    : km_(km), theme_(theme)
{
    update();
}

void CommandPalette::update()
{
    results_.clear();
    auto q = query_;
    for (auto &c : q)
    {
        c = static_cast<char>(std::tolower(c));
    }
    for (const auto &cmd : km_.commands())
    {
        std::string name = cmd.name;
        for (auto &ch : name)
        {
            ch = static_cast<char>(std::tolower(ch));
        }
        if (q.empty() || name.find(q) != std::string::npos)
        {
            results_.push_back(cmd.id);
        }
    }
}

bool CommandPalette::onEvent(const ui::Event &ev)
{
    using Code = KeyEvent::Code;
    if (ev.key.code == Code::Backspace)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            update();
        }
        return true;
    }
    if (ev.key.code == Code::Enter)
    {
        if (!results_.empty())
        {
            km_.execute(results_.front());
        }
        return true;
    }
    if (ev.key.code == Code::Unknown && ev.key.codepoint >= 32 && ev.key.codepoint <= 126)
    {
        query_.push_back(static_cast<char>(ev.key.codepoint));
        update();
        return true;
    }
    return false;
}

void CommandPalette::paint(render::ScreenBuffer &sb)
{
    const auto &st = theme_.style(style::Role::Normal);
    for (int y = 0; y < rect_.h; ++y)
    {
        for (int x = 0; x < rect_.w; ++x)
        {
            auto &cell = sb.at(rect_.y + y, rect_.x + x);
            cell.ch = U' ';
            cell.style = st;
        }
    }
    std::string header = ":" + query_;
    for (int x = 0; x < rect_.w && x < static_cast<int>(header.size()); ++x)
    {
        auto &cell = sb.at(rect_.y, rect_.x + x);
        cell.ch = static_cast<char32_t>(header[x]);
        cell.style = st;
    }
    int row = 1;
    for (const auto &id : results_)
    {
        if (row >= rect_.h)
        {
            break;
        }
        if (const auto *cmd = km_.find(id))
        {
            for (int x = 0; x < rect_.w && x < static_cast<int>(cmd->name.size()); ++x)
            {
                auto &cell = sb.at(rect_.y + row, rect_.x + x);
                cell.ch = static_cast<char32_t>(cmd->name[x]);
                cell.style = st;
            }
        }
        ++row;
    }
}

} // namespace viper::tui::widgets
