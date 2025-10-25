// tui/src/input/keymap.cpp
// @brief Keymap implementation dispatching commands from key chords.
// @invariant Widget bindings override global mappings.
// @ownership Keymap owns command callbacks; widget pointers are non-owning.

#include "tui/input/keymap.hpp"

namespace viper::tui::input
{
/// @brief Compare two key chords for matching code, modifiers, and codepoint.
bool KeyChord::operator==(const KeyChord &other) const
{
    return code == other.code && mods == other.mods && codepoint == other.codepoint;
}

/// @brief Compute a hash combining key code, modifiers, and Unicode codepoint.
std::size_t KeyChordHash::operator()(const KeyChord &kc) const
{
    return static_cast<std::size_t>(kc.code) ^ (static_cast<std::size_t>(kc.mods) << 8U) ^
           (static_cast<std::size_t>(kc.codepoint) << 16U);
}

void Keymap::registerCommand(CommandId id, std::string name, std::function<void()> action)
{
    auto it = index_.find(id);
    if (it != index_.end())
    {
        auto &cmd = commands_[it->second];
        cmd.name = std::move(name);
        cmd.action = std::move(action);
        return;
    }

    const auto slot = commands_.size();
    commands_.push_back(Command{std::move(id), std::move(name), std::move(action)});
    index_[commands_[slot].id] = slot;
}

void Keymap::bindGlobal(const KeyChord &kc, const CommandId &id)
{
    global_[kc] = id;
}

void Keymap::bindWidget(ui::Widget *w, const KeyChord &kc, const CommandId &id)
{
    widget_[w][kc] = id;
}

bool Keymap::execute(const CommandId &id) const
{
    auto it = index_.find(id);
    if (it != index_.end())
    {
        const auto &cmd = commands_[it->second];
        if (cmd.action)
        {
            cmd.action();
            return true;
        }
    }
    return false;
}

const Command *Keymap::find(const CommandId &id) const
{
    auto it = index_.find(id);
    if (it != index_.end())
    {
        return &commands_[it->second];
    }
    return nullptr;
}

bool Keymap::handle(ui::Widget *w, const term::KeyEvent &key) const
{
    KeyChord kc{key.code, key.mods, key.codepoint};
    if (w)
    {
        auto wit = widget_.find(w);
        if (wit != widget_.end())
        {
            auto cit = wit->second.find(kc);
            if (cit != wit->second.end())
            {
                return execute(cit->second);
            }
        }
    }
    auto git = global_.find(kc);
    if (git != global_.end())
    {
        return execute(git->second);
    }
    return false;
}

/// @brief Access the registered command metadata in insertion order.
const std::vector<Command> &Keymap::commands() const
{
    return commands_;
}

} // namespace viper::tui::input
