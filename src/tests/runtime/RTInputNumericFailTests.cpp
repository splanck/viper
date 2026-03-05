//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTInputNumericFailTests.cpp
// Purpose: Ensure INPUT-style numeric parsing traps when trailing junk appears.
// Key invariants: rt_to_double rejects non-whitespace suffixes and reports the INPUT trap.
// Ownership/Lifetime: Uses runtime helpers directly.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "common/ProcessIsolation.hpp"
#include <cassert>
#include <string>

static void call_input_12abc()
{
    rt_to_double(rt_const_cstr("12abc"));
}

static void call_input_7_5foo()
{
    rt_to_double(rt_const_cstr("7.5foo"));
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_input_12abc);
    bool trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    result = viper::tests::runIsolated(call_input_7_5foo);
    trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    return 0;
}
