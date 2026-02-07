//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Keymap class and supporting types for Viper's TUI
// command binding system. Keymap maps key chords (key + modifiers) to named
// commands, supporting both global bindings and per-widget bindings.
//
// Commands are registered with a unique string identifier, a display name,
// and a callback. Key chords are then bound to command identifiers at either
// the global scope or for a specific widget. When input arrives, the keymap
// checks widget-specific bindings first, then global bindings, executing
// the first matching command's callback.
//
// This two-level binding system enables context-sensitive shortcuts where
// a key combination can mean different things depending on the focused widget,
// while still supporting application-wide shortcuts.
//
// Key invariants:
//   - Command identifiers are unique strings within the keymap.
//   - Widget bindings take priority over global bindings.
//   - handle() returns true if a command was executed, false otherwise.
//
// Ownership: Keymap owns all Command entries and binding maps by value.
// Widget pointers in per-widget bindings are non-owning.
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

/// @brief Combination of a key code, modifier flags, and optional codepoint defining
///        a keyboard shortcut trigger.
/// @details Used as the key in binding maps. Two KeyChords are equal if all three
///          fields match. The codepoint field is used for character-based shortcuts
///          (e.g., Ctrl+S where codepoint = 's').
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

/// @brief Registered command with identifier, display name, and invocation callback.
/// @details Commands are the targets of key bindings. The id is used for programmatic
///          lookup, the name is displayed in the command palette, and the action
///          callback is invoked when the command is triggered.
struct Command
{
    CommandId id{};
    std::string name{};
    std::function<void()> action{};
};

/// @brief Two-level key binding system supporting global and per-widget command dispatch.
/// @details Manages a registry of named commands and maps key chords to command
///          identifiers. Supports both global bindings (active regardless of focus)
///          and widget-specific bindings (active only when a particular widget has focus).
///          Widget bindings take priority over global bindings when both match.
class Keymap
{
  public:
    /// @brief Register a new command with the keymap.
    /// @details Adds a command entry with a unique identifier, display name, and
    ///          action callback. Overwrites any existing command with the same id.
    /// @param id Unique string identifier for the command.
    /// @param name Human-readable name displayed in the command palette.
    /// @param action Callback invoked when the command is executed.
    void registerCommand(CommandId id, std::string name, std::function<void()> action);

    /// @brief Bind a key chord to a command at the global scope.
    /// @details Global bindings are checked when no widget-specific binding matches.
    /// @param kc The key chord that triggers the command.
    /// @param id The command identifier to execute when the chord is pressed.
    void bindGlobal(const KeyChord &kc, const CommandId &id);

    /// @brief Bind a key chord to a command for a specific widget.
    /// @details Widget bindings take priority over global bindings when the specified
    ///          widget has focus.
    /// @param w The widget this binding applies to. Must outlive the keymap.
    /// @param kc The key chord that triggers the command.
    /// @param id The command identifier to execute when the chord is pressed.
    void bindWidget(ui::Widget *w, const KeyChord &kc, const CommandId &id);

    /// @brief Attempt to handle a key event by finding and executing a bound command.
    /// @details Checks widget-specific bindings for the given widget first, then falls
    ///          back to global bindings. If a matching binding is found, the associated
    ///          command's action callback is invoked.
    /// @param w The currently focused widget (checked for widget-specific bindings).
    /// @param key The key event to match against bindings.
    /// @return True if a command was found and executed; false otherwise.
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
