//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/FrontBasicRejectBreakFlagTests.cpp
// Purpose: Ensure the BASIC front-end rejects unsupported debugger flags.
// Key invariants: `cmdFrontBasic` must fail fast on `--break` without invoking compilation.
// Ownership/Lifetime: Test owns argv buffers and captures usage state locally.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "tools/ilc/cli.hpp"

#include <cassert>

namespace
{

bool gUsageCalled = false;
bool gCompileCalled = false;

} // namespace

void usage()
{
    gUsageCalled = true;
}

#include "tools/ilc/cmd_front_basic.cpp"

namespace il::frontends::basic
{

void DiagnosticEmitter::printAll(std::ostream &) const {}

bool BasicCompilerResult::succeeded() const
{
    return false;
}

BasicCompilerResult compileBasic(const BasicCompilerInput &,
                                 const BasicCompilerOptions &,
                                 il::support::SourceManager &)
{
    gCompileCalled = true;
    return {};
}

} // namespace il::frontends::basic

int main()
{
    char run[] = "-run";
    char source[] = "dummy.bas";
    char breakFlag[] = "--break";
    char breakArg[] = "entry";

    char *argv[] = {run, source, breakFlag, breakArg};

    const int rc = cmdFrontBasic(4, argv);

    assert(rc != 0);
    assert(gUsageCalled);
    assert(!gCompileCalled);

    return 0;
}
