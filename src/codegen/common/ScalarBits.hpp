//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/ScalarBits.hpp
// Purpose: Shared scalar bit-pattern helpers used by native backends.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>

namespace zanna::codegen::common {

/// @brief Return the IEEE-754 bit pattern for a double without changing its value.
inline std::uint64_t f64Bits(double value) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace zanna::codegen::common
