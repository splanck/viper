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

static void call_input_12abc() {
    rt_to_double(rt_const_cstr("12abc"));
}

static void call_input_7_5foo() {
    rt_to_double(rt_const_cstr("7.5foo"));
}

static void call_input_int_nul_suffix() {
    const char bytes[] = {'1', '2', '3', '\0', 'j', 'u', 'n', 'k'};
    rt_string s = rt_string_from_bytes(bytes, sizeof(bytes));
    rt_to_int(s);
}

static void call_input_double_nul_suffix() {
    const char bytes[] = {'1', '.', '5', '\0', 'j', 'u', 'n', 'k'};
    rt_string s = rt_string_from_bytes(bytes, sizeof(bytes));
    rt_to_double(s);
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(call_input_12abc);
    viper::tests::registerChildFunction(call_input_7_5foo);
    viper::tests::registerChildFunction(call_input_int_nul_suffix);
    viper::tests::registerChildFunction(call_input_double_nul_suffix);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_input_12abc);
    bool trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    result = viper::tests::runIsolated(call_input_7_5foo);
    trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    result = viper::tests::runIsolated(call_input_int_nul_suffix);
    trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    result = viper::tests::runIsolated(call_input_double_nul_suffix);
    trapped = result.stderrText.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);

    return 0;
}
