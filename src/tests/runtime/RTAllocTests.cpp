//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAllocTests.cpp
// Purpose: Verify rt_alloc traps on negative allocation sizes.
// Key invariants: rt_alloc reports "negative allocation" when bytes < 0.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "rt.hpp"
#include <cassert>
#include <string>

static void call_alloc_negative()
{
    rt_alloc(-1);
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_alloc_negative);
    bool ok = result.stderrText.find("negative allocation") != std::string::npos;
    assert(ok);
    return 0;
}
