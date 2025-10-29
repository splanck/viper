// File: tests/unit/test_cli_run_invalid_max_steps.cpp
// Purpose: Verify --max-steps parsing rejects malformed values without throwing.
// Key invariants: Invalid max step specifications must trigger usage() and non-zero exit.
// Ownership/Lifetime: N/A.
// Links: src/tools/ilc/cli.cpp, src/tools/ilc/cmd_run_il.cpp

#include "tools/ilc/cli.hpp"

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

    gUsageCalled = false;
    int rc = invokeRunIL({"--max-steps", "not-a-number"}, err);
    assert(rc != 0);
    assert(gUsageCalled);

    gUsageCalled = false;
    err.clear();
    rc = invokeRunIL({"--max-steps", "18446744073709551616"}, err);
    assert(rc != 0);
    assert(gUsageCalled);

    gUsageCalled = false;
    err.clear();
    rc = invokeRunIL({"--max-steps", "-1"}, err);
    assert(rc != 0);
    assert(gUsageCalled);

    return 0;
}
