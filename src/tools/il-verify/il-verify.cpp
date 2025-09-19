// File: src/tools/il-verify/il-verify.cpp
// Purpose: Command-line tool verifying IL modules.
// Key invariants: None.
// Ownership/Lifetime: Tool owns parsed module.
// License: MIT License. See LICENSE for details.
// Links: docs/class-catalog.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <fstream>
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
    std::ifstream in(argv[1]);
    if (!in)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 1;
    }
    il::core::Module m;
    auto pe = il::api::v2::parse_text_expected(in, m);
    if (!pe)
    {
        il::support::printDiag(pe.error(), std::cerr);
        return 1;
    }
    auto ve = il::api::v2::verify_module_expected(m);
    if (!ve)
    {
        il::support::printDiag(ve.error(), std::cerr);
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
