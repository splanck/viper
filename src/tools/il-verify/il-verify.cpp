// File: src/tools/il-verify/il-verify.cpp
// Purpose: Command-line tool verifying IL modules.
// Key invariants: None.
// Ownership/Lifetime: Tool owns parsed module.
// License: MIT License. See LICENSE for details.
// Links: docs/codemap.md

#include "support/source_manager.hpp"
#include "tools/common/module_loader.hpp"
#include <iostream>
#include <string>

/// @brief CLI entry for verifying IL modules.
/// Usage:
///   il-verify <file.il>
///   il-verify --version
/// @param argc Number of CLI arguments.
/// @param argv Argument vector.
/// Internally handles argument validation, opens the input file, parses the
/// module, verifies it, and reports any diagnostics to stderr.
/// @return 0 on successful verification or when displaying the version;
///         1 on invalid usage, I/O, parse, or verification errors.
int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version")
    {
        std::cout << "IL v0.1.2\n";
        return 0;
    }
    if (argc != 2)
    {
        std::cerr << "Usage: il-verify <file.il>\n";
        return 1;
    }
    il::core::Module m;
    il::support::SourceManager sm;
    sm.addFile(argv[1]);
    auto load = il::tools::common::loadModuleFromFile(argv[1], m, std::cerr, "cannot open ");
    if (!load.succeeded())
    {
        return 1;
    }
    if (!il::tools::common::verifyModule(m, std::cerr, &sm))
    {
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
