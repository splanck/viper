// tui/include/tui/input/keymap.hpp
// @brief Map key chords to command callbacks with global and widget scopes.
// @invariant Key chords uniquely identify commands; widget overrides global.
// @ownership Keymap owns command list but not widget pointers.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tui/term/input.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::input
{

using CommandId = std::string;

/// @brief Key plus modifiers defining a command trigger.
struct KeyChord
{
    term::KeyEvent::Code code{term::KeyEvent::Code::Unknown};
    unsigned mods{0};
    uint32_t codepoint{0};

    bool operator==(const KeyChord &other) const;
};

struct KeyChordHash
{
    std::size_t operator()(const KeyChord &kc) const;
};

/// @brief Command entry with display name and callback.
struct Command
{
    CommandId id{};
    std::string name{};
    std::function<void()> action{};
};

/// @brief Keymap supporting global and widget scoped bindings.
class Keymap
{
  public:
    /// @brief Register a command with identifier, name, and callback.
    void registerCommand(CommandId id, std::string name, std::function<void()> action);

    /// @brief Bind a key chord to a command globally.
    void bindGlobal(const KeyChord &kc, const CommandId &id);

    /// @brief Bind a key chord to a command for specific widget.
    void bindWidget(ui::Widget *w, const KeyChord &kc, const CommandId &id);

    /// @brief Handle a key for a widget, executing mapped command.
    /// @return True if a command executed.
    bool handle(ui::Widget *w, const term::KeyEvent &key) const;

    /// @brief Execute command by identifier.
    bool execute(const CommandId &id) const;

    /// @brief Access registered commands.
    [[nodiscard]] const std::vector<Command> &commands() const;

    /// @brief Find command by id.
    [[nodiscard]] const Command *find(const CommandId &id) const;

  private:
    std::vector<Command> commands_{};
    std::unordered_map<CommandId, std::size_t> index_{};
    std::unordered_map<KeyChord, CommandId, KeyChordHash> global_{};
    std::unordered_map<ui::Widget *, std::unordered_map<KeyChord, CommandId, KeyChordHash>>
        widget_{};
};

} // namespace viper::tui::input
