// File: tests/unit/test_cli_run_break_src_line_padding.cpp
// Purpose: Ensure cmdRunIL accepts whitespace between colon and line digits.
// Key invariants: --break treats "file:  7" as a source breakpoint and hits it.
// Ownership/Lifetime: Reuses repository IL sample, no temp files.
// Links: src/tools/ilc/cmd_run_il.cpp

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
    std::vector<std::string> argStorage = { file, flag, spec };
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

void usage()
{
}

int main()
{
    const std::filesystem::path unitDir = std::filesystem::path(__FILE__).parent_path();
    const std::filesystem::path testsDir = unitDir.parent_path();
    const std::string ilFile = (testsDir / "e2e/BreakSrcLine7.bas").string();
    const std::string spec = ilFile + ":  7";
    std::string err;

    int rc = runWithArgs(ilFile, "--break", spec, err);
    assert(rc == 10);
    assert(err.find("[BREAK]") != std::string::npos);

    return 0;
}
