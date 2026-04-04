//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/binenc/X64Encoding.hpp
// Purpose: Maps PhysReg enum values to hardware register encoding (3-bit number
//          + REX extension bit) and provides condition-code-to-x86-CC-nibble
//          translation for the binary encoder.
// Key invariants:
//   - hwEncode() is constexpr and covers all 32 PhysReg values
//   - x86CC nibble values match Intel SDM Table B-1 (JCC/SETcc second opcode byte)
//   - SSE mandatory prefixes (F2/66) are emitted BEFORE REX, then 0F + opcode
// Ownership/Lifetime: Stateless; all functions are constexpr or inline.
// Links: codegen/x86_64/TargetX64.hpp
//        codegen/x86_64/binenc/X64BinaryEncoder.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/TargetX64.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace viper::codegen::x64::binenc {

/// Hardware register encoding: 3-bit number + REX extension bit.
struct HwReg {
    uint8_t bits3;  ///< Low 3 bits of register number (0-7).
    uint8_t rexBit; ///< 1 if register requires REX.R/B/X extension, 0 otherwise.
};

/// Map a PhysReg to its x86_64 hardware encoding.
///
/// The PhysReg enum order (RAX=0, RBX=1, RCX=2, ...) does NOT match
/// hardware (RAX=0, RCX=1, RDX=2, RBX=3, ...). This table bridges the gap.
inline constexpr HwReg hwEncode(PhysReg reg) {
    // Index: static_cast<int>(PhysReg)
    // Layout: {bits3, rexBit}
    constexpr HwReg kTable[] = {
        {0, 0}, // RAX  = 0
        {3, 0}, // RBX  = 1
        {1, 0}, // RCX  = 2
        {2, 0}, // RDX  = 3
        {6, 0}, // RSI  = 4
        {7, 0}, // RDI  = 5
        {0, 1}, // R8   = 6
        {1, 1}, // R9   = 7
        {2, 1}, // R10  = 8
        {3, 1}, // R11  = 9
        {4, 1}, // R12  = 10
        {5, 1}, // R13  = 11
        {6, 1}, // R14  = 12
        {7, 1}, // R15  = 13
        {5, 0}, // RBP  = 14
        {4, 0}, // RSP  = 15
        {0, 0}, // XMM0  = 16
        {1, 0}, // XMM1  = 17
        {2, 0}, // XMM2  = 18
        {3, 0}, // XMM3  = 19
        {4, 0}, // XMM4  = 20
        {5, 0}, // XMM5  = 21
        {6, 0}, // XMM6  = 22
        {7, 0}, // XMM7  = 23
        {0, 1}, // XMM8  = 24
        {1, 1}, // XMM9  = 25
        {2, 1}, // XMM10 = 26
        {3, 1}, // XMM11 = 27
        {4, 1}, // XMM12 = 28
        {5, 1}, // XMM13 = 29
        {6, 1}, // XMM14 = 30
        {7, 1}, // XMM15 = 31
    };
    return kTable[static_cast<int>(reg)];
}

/// Map a Viper condition code (0-13) to the x86_64 CC nibble used in
/// JCC (0F 8x) and SETcc (0F 9x) second opcode bytes.
///
/// The mapping follows the Intel SDM condition code encoding:
///   Viper 0 (eq)  -> x86 4 (E)     Viper 7 (ae) -> x86 3 (AE/NB)
///   Viper 1 (ne)  -> x86 5 (NE)    Viper 8 (b)  -> x86 2 (B/C)
///   Viper 2 (lt)  -> x86 C (L)     Viper 9 (be) -> x86 6 (BE/NA)
///   Viper 3 (le)  -> x86 E (LE)    Viper 10 (p) -> x86 A (P/PE)
///   Viper 4 (gt)  -> x86 F (G)     Viper 11 (np)-> x86 B (NP/PO)
///   Viper 5 (ge)  -> x86 D (GE)    Viper 12 (o) -> x86 0 (O)
///   Viper 6 (a)   -> x86 7 (A/NBE) Viper 13 (no)-> x86 1 (NO)
inline constexpr uint8_t x86CC(int viperCC) {
    constexpr uint8_t kTable[] = {
        0x4, // 0: eq  -> E
        0x5, // 1: ne  -> NE
        0xC, // 2: lt  -> L
        0xE, // 3: le  -> LE
        0xF, // 4: gt  -> G
        0xD, // 5: ge  -> GE
        0x7, // 6: a   -> A
        0x3, // 7: ae  -> AE
        0x2, // 8: b   -> B
        0x6, // 9: be  -> BE
        0xA, // 10: p  -> P
        0xB, // 11: np -> NP
        0x0, // 12: o  -> O
        0x1, // 13: no -> NO
    };
    assert(viperCC >= 0 && viperCC <= 13);
    return kTable[viperCC];
}

// === ModR/M + SIB construction helpers ===

/// Build a ModR/M byte: mod(2) | reg(3) | rm(3).
inline constexpr uint8_t makeModRM(uint8_t mod, uint8_t reg3, uint8_t rm3) {
    return static_cast<uint8_t>((mod << 6) | ((reg3 & 7) << 3) | (rm3 & 7));
}

/// Build a SIB byte: scale(2) | index(3) | base(3).
inline constexpr uint8_t makeSIB(uint8_t scale, uint8_t index3, uint8_t base3) {
    return static_cast<uint8_t>((scale << 6) | ((index3 & 7) << 3) | (base3 & 7));
}

/// Compute the SIB scale encoding from an integer scale factor.
///   1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3.
inline uint8_t scaleLog2(uint8_t scale) {
    switch (scale) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        default:
            throw std::invalid_argument("x86-64 binary encoder: invalid SIB scale factor");
    }
}

/// Compute a REX prefix byte (0x40 | W<<3 | R<<2 | X<<1 | B).
/// Returns 0 if no REX byte is needed.
inline constexpr uint8_t computeRex(bool w, bool r, bool x, bool b) {
    uint8_t rex = 0x40;
    if (w)
        rex |= 0x08;
    if (r)
        rex |= 0x04;
    if (x)
        rex |= 0x02;
    if (b)
        rex |= 0x01;
    return rex;
}

/// Whether a REX prefix is needed given the WRXB flags.
inline constexpr bool needsRex(bool w, bool r, bool x, bool b) {
    return w || r || x || b;
}

// === RegReg ALU opcode table ===

/// Primary opcode byte for reg-reg ALU instructions.
/// Indexed by MOpcode enum value for the subset of RegReg instructions.
/// Returns 0 for opcodes that don't use this simple pattern.
struct RegRegOp {
    uint8_t primary;   ///< Primary opcode byte (or 0x0F escape prefix).
    uint8_t secondary; ///< Secondary byte after 0F (0 if single-byte opcode).
    bool regIsDst;     ///< True if ModR/M reg field is the destination.
};

/// Lookup the opcode bytes for a reg-reg GPR instruction.
inline constexpr RegRegOp regRegOpcode(MOpcode op) {
    switch (op) {
        case MOpcode::MOVrr:
            return {0x89, 0, false}; // reg=src, r/m=dst
        case MOpcode::ADDrr:
            return {0x01, 0, false}; // reg=src, r/m=dst
        case MOpcode::SUBrr:
            return {0x29, 0, false};
        case MOpcode::ANDrr:
            return {0x21, 0, false};
        case MOpcode::ORrr:
            return {0x09, 0, false};
        case MOpcode::XORrr:
            return {0x31, 0, false};
        case MOpcode::CMPrr:
            return {0x39, 0, false};
        case MOpcode::TESTrr:
            return {0x85, 0, false};
        case MOpcode::IMULrr:
            return {0x0F, 0xAF, true}; // reg=dst, r/m=src
        case MOpcode::CMOVNErr:
            return {0x0F, 0x45, true}; // reg=dst, r/m=src
        case MOpcode::XORrr32:
            return {0x31, 0, false};
        default:
            assert(false && "not a reg-reg GPR opcode");
            return {0, 0, false};
    }
}

/// /ext field in ModR/M reg bits for reg-imm ALU (opcode 81/83).
inline constexpr uint8_t regImmExt(MOpcode op) {
    switch (op) {
        case MOpcode::ADDri:
            return 0; // /0
        case MOpcode::ORri:
            return 1; // /1
        case MOpcode::ANDri:
            return 4; // /4
        case MOpcode::XORri:
            return 6; // /6
        case MOpcode::CMPri:
            return 7; // /7
        default:
            assert(false && "not a reg-imm ALU opcode");
            return 0;
    }
}

/// /ext field in ModR/M reg bits for shift instructions (opcode C1/D3).
inline constexpr uint8_t shiftExt(MOpcode op) {
    switch (op) {
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
            return 4; // /4
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
            return 5; // /5
        case MOpcode::SARri:
        case MOpcode::SARrc:
            return 7; // /7
        default:
            assert(false && "not a shift opcode");
            return 0;
    }
}

/// SSE instruction descriptor for scalar double operations.
struct SseOp {
    uint8_t prefix; ///< Mandatory prefix: 0xF2, 0x66, or 0 (none for MOVUPS).
    uint8_t opcode; ///< Opcode byte after 0F.
    bool regIsDst;  ///< True if ModR/M reg field is the destination.
    bool needsRexW; ///< True if REX.W is needed (CVTSI2SD, CVTTSD2SI, MOVQrx, MOVQxr).
};

inline constexpr SseOp sseOpcode(MOpcode op) {
    switch (op) {
        case MOpcode::FADD:
            return {0xF2, 0x58, true, false};
        case MOpcode::FSUB:
            return {0xF2, 0x5C, true, false};
        case MOpcode::FMUL:
            return {0xF2, 0x59, true, false};
        case MOpcode::FDIV:
            return {0xF2, 0x5E, true, false};
        case MOpcode::UCOMIS:
            return {0x66, 0x2E, true, false};
        case MOpcode::CVTSI2SD:
            return {0xF2, 0x2A, true, true};
        case MOpcode::CVTTSD2SI:
            return {0xF2, 0x2C, true, true};
        case MOpcode::MOVQrx:
            return {0x66, 0x6E, true, true};
        case MOpcode::MOVQxr:
            return {0x66, 0x7E, false, true};
        case MOpcode::MOVSDrr:
            return {0xF2, 0x10, true, false};
        case MOpcode::MOVSDrm:
            return {0xF2, 0x11, false, false}; // store: reg=src
        case MOpcode::MOVSDmr:
            return {0xF2, 0x10, true, false}; // load: reg=dst
        case MOpcode::MOVUPSrm:
            return {0x00, 0x11, false, false}; // store: no prefix
        case MOpcode::MOVUPSmr:
            return {0x00, 0x10, true, false}; // load: no prefix
        default:
            assert(false && "not an SSE opcode");
            return {0, 0, false, false};
    }
}

} // namespace viper::codegen::x64::binenc
