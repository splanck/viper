//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/cmd_repl.cpp
// Purpose: Entry point for the `viper repl` subcommand. Creates a REPL
//          session with the appropriate language adapter and runs it.
// Key invariants:
//   - Default language is Zia.
//   - Supports both Zia and BASIC adapters.
//   - Exits with the session's exit code.
// Ownership/Lifetime:
//   - ReplSession owns the adapter and runs for the lifetime of the command.
// Links: src/repl/ReplSession.hpp, src/repl/ZiaReplAdapter.hpp,
//        src/repl/BasicReplAdapter.hpp
//
//===----------------------------------------------------------------------===//

#include "repl/BasicReplAdapter.hpp"
#include "repl/ReplSession.hpp"
#include "repl/ZiaReplAdapter.hpp"

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

/// @brief Entry point for `viper repl [zia|basic]`.
/// @param argc Argument count (after "repl" is stripped).
/// @param argv Argument vector.
/// @return Exit code from the REPL session.
int cmdRepl(int argc, char **argv) {
    std::string lang = "zia"; // Default language

    // Parse optional language argument
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "zia") == 0) {
            lang = "zia";
        } else if (std::strcmp(argv[i], "basic") == 0) {
            lang = "basic";
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: viper repl [zia|basic]\n"
                      << "  Launch an interactive REPL session.\n"
                      << "  Default language: zia\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argv[i] << "\n";
            std::cerr << "Usage: viper repl [zia|basic]\n";
            return 1;
        }
    }

    std::unique_ptr<viper::repl::ReplAdapter> adapter;

    if (lang == "basic") {
        adapter = std::make_unique<viper::repl::BasicReplAdapter>();
    } else {
        adapter = std::make_unique<viper::repl::ZiaReplAdapter>();
    }

    viper::repl::ReplSession session(std::move(adapter));
    return session.run();
}
