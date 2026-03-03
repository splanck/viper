//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplMetaCommands.hpp
// Purpose: Registry of dot-prefixed meta-commands (.help, .quit, .vars, etc.)
//          for the Viper REPL.
// Key invariants:
//   - Command names are stored without the leading dot.
//   - tryHandle() returns false if the input is not a meta-command.
// Ownership/Lifetime:
//   - Owns the handler map; handlers are std::function objects.
// Links: src/repl/ReplSession.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace viper::repl
{

// Forward declaration
class ReplSession;

/// @brief A single meta-command entry.
struct MetaCommandEntry
{
    std::string name;                                        ///< Command name (without dot).
    std::string help;                                        ///< Short help description.
    std::function<void(ReplSession &, const std::string &)> handler; ///< Handler receiving args.
};

/// @brief Registry and dispatcher for REPL meta-commands.
/// @details Meta-commands are dot-prefixed (e.g., ".help", ".quit") and are
///          dispatched before any language compilation. The registry supports
///          multiple aliases for the same command (e.g., .quit and .exit).
class ReplMetaCommands
{
  public:
    /// @brief Register a new meta-command.
    /// @param name Command name (without leading dot).
    /// @param help Short description shown in .help output.
    /// @param handler Function called with (session, remaining_args).
    void registerCommand(const std::string &name, const std::string &help,
                         std::function<void(ReplSession &, const std::string &)> handler);

    /// @brief Try to handle input as a meta-command.
    /// @param input The raw input string (should start with '.').
    /// @param session The REPL session to pass to the handler.
    /// @return True if the input was recognized and handled as a meta-command.
    bool tryHandle(const std::string &input, ReplSession &session);

    /// @brief Print help text listing all registered commands.
    void printHelp() const;

  private:
    std::vector<MetaCommandEntry> commands_;
};

} // namespace viper::repl
