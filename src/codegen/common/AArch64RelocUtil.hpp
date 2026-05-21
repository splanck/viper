//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/AArch64RelocUtil.hpp
// Purpose: Shared AArch64 instruction-shape helpers for relocation validation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace viper::codegen {

inline bool isA64AddImmediate(uint32_t insn) {
    return (insn & 0x7F000000u) == 0x11000000u;
}

inline bool a64UnsignedLdStOffsetShift(uint32_t insn, uint32_t &shift) {
    // Unsigned-immediate load/store class. This excludes pre/post-indexed,
    // unscaled, literal, and pair encodings; the scale lives in bits [31:30],
    // with 128-bit SIMD/vector memory operations using the architectural
    // 16-byte scale.
    if ((insn & 0x3B000000u) != 0x39000000u)
        return false;
    shift = insn >> 30;
    if ((insn & 0x04800000u) == 0x04800000u)
        shift = 4;
    return true;
}

inline bool isA64UnsignedLdStOffsetWithShift(uint32_t insn, uint32_t expectedShift) {
    uint32_t shift = 0;
    return a64UnsignedLdStOffsetShift(insn, shift) && shift == expectedShift;
}

} // namespace viper::codegen
