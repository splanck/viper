//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringRangeTests.cpp
// Purpose: Verify runtime string helpers report negative start/length diagnostics.
// Key invariants: LEFT$ and MID$ trap with specific messages on invalid ranges.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "rt.hpp"
#include <cassert>
#include <string>

static void call_left_negative() {
    rt_str_left(rt_const_cstr("A"), -1);
}

static void call_mid_negative() {
    rt_str_mid_len(rt_const_cstr("A"), -1, 1);
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(call_left_negative);
    viper::tests::registerChildFunction(call_mid_negative);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_left_negative);
    bool ok = result.stderrText.find("LEFT$: len must be >= 0") != std::string::npos;
    assert(ok);

    result = viper::tests::runIsolated(call_mid_negative);
    ok = result.stderrText.find("MID$: start must be >= 1") != std::string::npos;
    assert(ok);

    rt_string sample = rt_const_cstr("ABCDEF");
    rt_string start_one = rt_str_mid(sample, 1);
    assert(rt_str_eq(start_one, sample));

    rt_string start_len = rt_str_mid(sample, 6);
    assert(rt_str_eq(start_len, rt_const_cstr("F")));

    rt_string start_len_with_count = rt_str_mid_len(sample, 6, 5);
    assert(rt_str_eq(start_len_with_count, rt_const_cstr("F")));

    rt_string start_beyond = rt_str_mid_len(sample, 7, 3);
    assert(rt_str_eq(start_beyond, rt_str_empty()));

    return 0;
}
