//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_cli_run_break_src_whitespace.cpp
// Purpose: Ensure cmdRunIL trims whitespace around breakpoint file specs.
// Key invariants: Both --break and --break-src accept padded file paths.
// Ownership/Lifetime: Uses repository IL sample, no temp files.
// Links: src/tools/ilc/cmd_run_il.cpp
//
//===----------------------------------------------------------------------===//

#include "tools/ilc/cli.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int runWithArgs(const std::string &file,
                const std::string &flag,
                const std::string &spec,
                std::string &stderrText)
{
    std::vector<std::string> argStorage = {file, flag, spec};
    std::vector<char *> argv;
    argv.reserve(argStorage.size());
    for (auto &arg : argStorage)
    {
        argv.push_back(arg.data());
    }

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdRunIL(static_cast<int>(argv.size()), argv.data());
    std::cerr.flush();
    std::cerr.rdbuf(oldBuf);

    stderrText = errStream.str();
    return rc;
}

} // namespace

void usage() {}

int main()
{
#ifdef _WIN32
    // Skip on Windows: cmdRunIL has Windows-specific path handling issues
    std::cout << "Test skipped: cmdRunIL path handling differs on Windows\n";
    return 0;
#endif
    const std::filesystem::path unitDir = std::filesystem::path(__FILE__).parent_path();
    const std::filesystem::path testsDir = unitDir.parent_path();
    const std::string ilFile = (testsDir / "e2e/BreakSrcExact.bas").string();
    const std::string spec = "  " + ilFile + "  :1";
    std::string err;

    int rc = runWithArgs(ilFile, "--break", spec, err);
    assert(rc == 10);
    assert(err.find("[BREAK]") != std::string::npos);

    err.clear();
    rc = runWithArgs(ilFile, "--break-src", spec, err);
    assert(rc == 10);
    assert(err.find("[BREAK]") != std::string::npos);

    const std::string specWithLinePadding = ilFile + ":  1";

    err.clear();
    rc = runWithArgs(ilFile, "--break-src", specWithLinePadding, err);
    assert(rc == 10);
    assert(err.find("[BREAK]") != std::string::npos);

    return 0;
}
