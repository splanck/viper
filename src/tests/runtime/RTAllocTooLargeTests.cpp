//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAllocTooLargeTests.cpp
// Purpose: Verify rt_alloc traps when allocation exceeds size_t range.
// Key invariants: rt_alloc reports "allocation too large" when bytes > SIZE_MAX.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "rt.hpp"
#include <cassert>
#include <stdint.h>
#include <string>

#if SIZE_MAX < INT64_MAX

static void call_alloc_too_large()
{
    rt_alloc((int64_t)SIZE_MAX + 1);
}

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    auto result = viper::tests::runIsolated(call_alloc_too_large);
    bool ok = result.stderrText.find("allocation too large") != std::string::npos;
    assert(ok);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
