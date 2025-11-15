// File: tests/unit/test_cli_run_invalid_break_line.cpp
// Purpose: Ensure cmdRunIL gracefully rejects malformed break line numbers.
// Key invariants: Invalid --break/--break-src arguments must report usage and fail.
// Ownership/Lifetime: N/A.
// Links: src/tools/ilc/cmd_run_il.cpp

#include "tools/ilc/cli.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int invokeWithFlag(const std::string &flag, const std::string &spec, std::string &stderrText)
{
    std::string fileArg = "placeholder.il";
    std::vector<std::string> argStorage = {fileArg, flag, spec};
    std::vector<char *> argv = {argStorage[0].data(), argStorage[1].data(), argStorage[2].data()};

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdRunIL(static_cast<int>(argv.size()), argv.data());
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
    int rc = invokeWithFlag("--break-src", "tests/e2e/BreakSrcExact.bas:not-a-number", err);
    assert(rc != 0);
    assert(gUsageCalled);
    assert(err.find("invalid line number") != std::string::npos);
    assert(err.find("--break-src") != std::string::npos);

    gUsageCalled = false;
    err.clear();
    rc = invokeWithFlag("--break-src", ":42", err);
    assert(rc != 0);
    assert(gUsageCalled);
    assert(err.find("invalid line number") != std::string::npos);
    assert(err.find("--break-src") != std::string::npos);

    gUsageCalled = false;
    err.clear();
    rc = invokeWithFlag("--break", "tests/e2e/BreakSrcExact.bas:99999999999999999999", err);
    assert(rc != 0);
    assert(gUsageCalled);
    assert(err.find("invalid line number") != std::string::npos);
    assert(err.find("--break") != std::string::npos);

    gUsageCalled = false;
    err.clear();
    rc = invokeWithFlag("--break", ":5", err);
    assert(rc != 0);
    assert(gUsageCalled);
    assert(err.find("invalid line number") != std::string::npos);
    assert(err.find("--break") != std::string::npos);

    return 0;
}
