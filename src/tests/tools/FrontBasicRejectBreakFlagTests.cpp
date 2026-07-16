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
// Links: docs/internals/testing.md
//
//===----------------------------------------------------------------------===//

#include "tools/viper/cli.hpp"

#include <cassert>

namespace {

bool gCompileCalled = false;

} // namespace

#include "tools/viper/cmd_front_basic.cpp"

namespace il::frontends::basic {

// NOTE: DiagnosticEmitter::printAll is deliberately NOT stubbed here. The
// real definition's archive member (DiagnosticEmitter.cpp.o) gets pulled
// into this link by other references, and a local stub would be a duplicate
// strong symbol (the ASan/Debug build diagnosed exactly that). The real
// printAll on an emitter with no recorded diagnostics prints nothing, which
// is all this mock flow needs.

bool BasicCompilerResult::succeeded() const {
    return false;
}

BasicCompilerResult compileBasic(const BasicCompilerInput &,
                                 const BasicCompilerOptions &,
                                 il::support::SourceManager &) {
    gCompileCalled = true;
    return {};
}

} // namespace il::frontends::basic

int main() {
    char run[] = "-run";
    char source[] = "dummy.bas";
    char breakFlag[] = "--break";
    char breakArg[] = "entry";

    char *argv[] = {run, source, breakFlag, breakArg};

    const int rc = cmdFrontBasic(4, argv);

    assert(rc != 0);
    assert(!gCompileCalled);

    return 0;
}
