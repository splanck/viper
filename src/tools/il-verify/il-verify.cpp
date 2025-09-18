// File: src/tools/il-verify/il-verify.cpp
// Purpose: Command-line tool verifying IL modules.
// Key invariants: None.
// Ownership/Lifetime: Tool owns parsed module.
// Links: docs/class-catalog.md

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

/// @brief CLI entry for verifying IL modules.
/// Usage:
///   il-verify <file.il>
///   il-verify --version
/// @param argc Number of CLI arguments.
/// @param argv Argument vector.
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
    if (!il::io::Parser::parse(in, m, std::cerr))
        return 1;
    std::ostringstream diag;
    if (!il::verify::Verifier::verify(m, diag))
    {
        std::cerr << diag.str();
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
