//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTChrInvalidTests.cpp
// Purpose: Ensure rt_chr traps on out-of-range input.
// Key invariants: Codes outside 0-255 trigger runtime trap.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "common/ProcessIsolation.hpp"
#include <cassert>
#include <string>

static void call_chr_negative()
{
    rt_str_chr(-1);
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_chr_negative);
    bool ok = result.stderrText.find("CHR$: code must be 0-255") != std::string::npos;
    assert(ok);
    return 0;
}
