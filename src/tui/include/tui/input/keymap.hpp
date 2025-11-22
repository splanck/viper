//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/input/keymap.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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
