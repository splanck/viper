//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/common/NetworkTestCompat.hpp
// Purpose: Shared constants for portable network tests.
// Key invariants:
//   - IPv4 constants are expressed in host byte order.
//   - The header does not initialize or own native socket state.
// Ownership/Lifetime:
//   - No resources are allocated by this helper.
//   - Callers remain responsible for native socket lifetime.
// Links: src/tests/runtime/RTNetworkTests.cpp,
//        src/tests/runtime/RTNetworkTimeoutTests.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace viper::tests {

static constexpr uint32_t kIpv4LoopbackHostOrder = 0x7f000001u;

} // namespace viper::tests
