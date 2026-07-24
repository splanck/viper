//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_platform_capabilities.cpp
// Purpose: Compile-time and runtime coverage for generated host capabilities.
//
// Key invariants:
//   - Exactly one supported host capability is enabled.
//   - Macro and constexpr views describe the same host.
//
// Ownership/Lifetime:
//   - Standalone test with no retained state.
//
// Links: include/zanna/platform/Capabilities.hpp.in
//
//===----------------------------------------------------------------------===//

#include "common/PlatformCapabilities.hpp"

#include <cassert>

static_assert(ZANNA_HOST_WINDOWS + ZANNA_HOST_MACOS + ZANNA_HOST_LINUX == 1);
static_assert(zanna::platform::kHostWindows == (ZANNA_HOST_WINDOWS != 0));
static_assert(zanna::platform::kHostMacOS == (ZANNA_HOST_MACOS != 0));
static_assert(zanna::platform::kHostLinux == (ZANNA_HOST_LINUX != 0));

int main() {
    const int host_count = static_cast<int>(zanna::platform::kHostWindows) +
                           static_cast<int>(zanna::platform::kHostMacOS) +
                           static_cast<int>(zanna::platform::kHostLinux);
    assert(host_count == 1);
    return 0;
}
