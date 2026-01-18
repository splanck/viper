//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_cli_run_bounds_checks_unsupported.cpp
// Purpose: Ensure ilc run rejects --bounds-checks with a clear diagnostic.
// Key invariants: Unsupported flag must emit an explanatory error without invoking usage().
// Ownership/Lifetime: N/A.
// Links: src/tools/ilc/cmd_run_il.cpp, src/tools/ilc/cli.cpp
//
//===----------------------------------------------------------------------===//

#include "tools/viper/cli.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int invokeRunIL(const std::vector<std::string> &extraArgs, std::string &stderrText)
{
    std::vector<std::string> storage;
    storage.emplace_back("placeholder.il");
    storage.insert(storage.end(), extraArgs.begin(), extraArgs.end());

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (auto &arg : storage)
    {
        argv.push_back(arg.data());
    }

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    const int rc = cmdRunIL(static_cast<int>(argv.size()), argv.data());
    std::cerr.flush();
    std::cerr.rdbuf(oldBuf);

    stderrText = errStream.str();
    return rc;
}

} // namespace

static bool gUsageCalled = false;

void usage()
{
    gUsageCalled = true;
}

int main()
{
    std::string err;
    const int rc = invokeRunIL({"--bounds-checks"}, err);

    const bool mentionsUnsupported =
        err.find("--bounds-checks is not supported") != std::string::npos;

    assert(rc != 0);
    assert(!gUsageCalled);
    assert(mentionsUnsupported);
    return 0;
}
