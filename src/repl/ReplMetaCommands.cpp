//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplMetaCommands.cpp
// Purpose: Implementation of meta-command registry and dispatch.
// Key invariants:
//   - Input must start with '.' to be considered a meta-command.
//   - Command names are matched case-insensitively.
// Ownership/Lifetime:
//   - Commands_ vector is owned by ReplMetaCommands.
// Links: src/repl/ReplMetaCommands.hpp
//
//===----------------------------------------------------------------------===//

#include "ReplMetaCommands.hpp"
#include "ReplColorScheme.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace viper::repl
{

void ReplMetaCommands::registerCommand(
    const std::string &name,
    const std::string &help,
    std::function<void(ReplSession &, const std::string &)> handler)
{
    commands_.push_back({name, help, std::move(handler)});
}

bool ReplMetaCommands::tryHandle(const std::string &input, ReplSession &session)
{
    if (input.empty() || input[0] != '.')
        return false;

    // Parse: ".command args..."
    size_t cmdStart = 1;
    size_t cmdEnd = cmdStart;
    while (cmdEnd < input.size() && !std::isspace(static_cast<unsigned char>(input[cmdEnd])))
        ++cmdEnd;

    std::string cmdName = input.substr(cmdStart, cmdEnd - cmdStart);

    // Convert to lowercase for case-insensitive matching
    std::transform(cmdName.begin(),
                   cmdName.end(),
                   cmdName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Extract args (trim leading whitespace)
    std::string args;
    size_t argsStart = cmdEnd;
    while (argsStart < input.size() && std::isspace(static_cast<unsigned char>(input[argsStart])))
        ++argsStart;
    if (argsStart < input.size())
        args = input.substr(argsStart);

    // Find and execute matching command
    for (const auto &cmd : commands_)
    {
        if (cmd.name == cmdName)
        {
            cmd.handler(session, args);
            return true;
        }
    }

    // Unknown meta-command
    std::cout << colors::error() << "Unknown command: ." << cmdName << colors::reset() << "\n";
    std::cout << "Type " << colors::bold() << ".help" << colors::reset()
              << " for available commands.\n";
    return true;
}

void ReplMetaCommands::printHelp() const
{
    std::cout << colors::bold() << "Available commands:" << colors::reset() << "\n";

    // Find max command name length for alignment
    size_t maxLen = 0;
    for (const auto &cmd : commands_)
        maxLen = std::max(maxLen, cmd.name.size());

    for (const auto &cmd : commands_)
    {
        std::cout << "  " << colors::prompt() << "." << cmd.name << colors::reset();
        // Pad to align descriptions
        for (size_t i = cmd.name.size(); i < maxLen + 2; ++i)
            std::cout << ' ';
        std::cout << cmd.help << "\n";
    }
}

} // namespace viper::repl
