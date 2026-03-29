//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringAllocOverflowTests.cpp
// Purpose: Ensure rt_string_alloc traps when length+1 would overflow.
// Key invariants: Runtime string allocation guards against size_t overflow.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include "common/ProcessIsolation.hpp"
#include <cassert>
#include <stdint.h>
#include <string>

static void call_string_len_overflow() {
    rt_string_from_bytes(NULL, SIZE_MAX);
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(call_string_len_overflow);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_string_len_overflow);
    bool ok = result.stderrText.find("rt_string_alloc: length overflow") != std::string::npos;
    assert(ok);
    return 0;
}
