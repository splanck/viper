//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/binenc/A64Encoding.hpp
// Purpose: AArch64 instruction encoding constants, templates, and helpers for
//          the binary encoder. Provides 32-bit instruction templates for each
//          opcode class, register-to-hardware mapping, and condition code encoding.
// Key invariants:
//   - Every AArch64 instruction is exactly 32 bits (4 bytes), little-endian
//   - PhysReg enum values map directly to hardware encoding for GPRs (0-31)
//   - FPR hardware encoding = PhysReg enum value - 32
//   - Condition codes are 4-bit values; inversion flips the LSB
// Ownership/Lifetime:
//   - All functions are constexpr/inline; no runtime state
// Links: codegen/aarch64/TargetAArch64.hpp
//        plans/03-aarch64-binary-encoder.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/TargetAArch64.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace viper::codegen::aarch64::binenc
{

// =============================================================================
// Register Encoding
// =============================================================================

/// Map a GPR PhysReg to its 5-bit hardware encoding (0-31).
constexpr uint32_t hwGPR(PhysReg r)
{
    assert(static_cast<uint32_t>(r) <= static_cast<uint32_t>(PhysReg::SP));
    return static_cast<uint32_t>(r);
}

/// Map an FPR PhysReg (V0-V31) to its 5-bit hardware encoding (0-31).
constexpr uint32_t hwFPR(PhysReg r)
{
    assert(static_cast<uint32_t>(r) >= static_cast<uint32_t>(PhysReg::V0));
    assert(static_cast<uint32_t>(r) <= static_cast<uint32_t>(PhysReg::V31));
    return static_cast<uint32_t>(r) - static_cast<uint32_t>(PhysReg::V0);
}

// =============================================================================
// Condition Code Encoding
// =============================================================================

/// Map a condition code string ("eq", "ne", "lt", etc.) to its 4-bit ARM value.
/// Returns 0xE (always) for unrecognized codes.
inline uint32_t condCode(const char *cond)
{
    if (!cond) return 0xE;
    // Fast 2-char comparison using memcmp
    struct Entry { const char str[3]; uint32_t code; };
    static constexpr Entry table[] = {
        {"eq", 0x0}, {"ne", 0x1}, {"hs", 0x2}, {"cs", 0x2},
        {"lo", 0x3}, {"cc", 0x3}, {"mi", 0x4}, {"pl", 0x5},
        {"vs", 0x6}, {"vc", 0x7}, {"hi", 0x8}, {"ls", 0x9},
        {"ge", 0xA}, {"lt", 0xB}, {"gt", 0xC}, {"le", 0xD},
        {"al", 0xE}, {"nv", 0xF},
    };
    for (const auto &e : table)
        if (std::strcmp(cond, e.str) == 0) return e.code;
    return 0xE;
}

/// Invert a 4-bit condition code (flip LSB).
constexpr uint32_t invertCond(uint32_t cc) { return cc ^ 1; }

// =============================================================================
// Instruction Templates — Data Processing (Three-Register)
// =============================================================================

inline constexpr uint32_t kAddRRR  = 0x8B000000; // add  Xd, Xn, Xm
inline constexpr uint32_t kSubRRR  = 0xCB000000; // sub  Xd, Xn, Xm
inline constexpr uint32_t kAndRRR  = 0x8A000000; // and  Xd, Xn, Xm
inline constexpr uint32_t kOrrRRR  = 0xAA000000; // orr  Xd, Xn, Xm
inline constexpr uint32_t kEorRRR  = 0xCA000000; // eor  Xd, Xn, Xm
inline constexpr uint32_t kAddsRRR = 0xAB000000; // adds Xd, Xn, Xm
inline constexpr uint32_t kSubsRRR = 0xEB000000; // subs Xd, Xn, Xm
inline constexpr uint32_t kAndsRRR = 0xEA000000; // ands Xd, Xn, Xm (for TST alias)

// =============================================================================
// Instruction Templates — Variable Shift
// =============================================================================

inline constexpr uint32_t kLslvRRR = 0x9AC02000; // lslv Xd, Xn, Xm
inline constexpr uint32_t kLsrvRRR = 0x9AC02400; // lsrv Xd, Xn, Xm
inline constexpr uint32_t kAsrvRRR = 0x9AC02800; // asrv Xd, Xn, Xm

// =============================================================================
// Instruction Templates — Multiply / Divide
// =============================================================================

inline constexpr uint32_t kMulRRR   = 0x9B007C00; // madd Xd, Xn, Xm, XZR (mul alias)
inline constexpr uint32_t kSmulhRRR = 0x9B407C00; // smulh Xd, Xn, Xm
inline constexpr uint32_t kSDivRRR  = 0x9AC00C00; // sdiv Xd, Xn, Xm
inline constexpr uint32_t kUDivRRR  = 0x9AC00800; // udiv Xd, Xn, Xm
inline constexpr uint32_t kMSubRRRR = 0x9B008000; // msub Xd, Xn, Xm, Xa
inline constexpr uint32_t kMAddRRRR = 0x9B000000; // madd Xd, Xn, Xm, Xa

// =============================================================================
// Instruction Templates — Immediate (Add/Sub)
// =============================================================================

inline constexpr uint32_t kAddRI  = 0x91000000; // add  Xd, Xn, #imm12
inline constexpr uint32_t kSubRI  = 0xD1000000; // sub  Xd, Xn, #imm12
inline constexpr uint32_t kAddsRI = 0xB1000000; // adds Xd, Xn, #imm12
inline constexpr uint32_t kSubsRI = 0xF1000000; // subs Xd, Xn, #imm12

// =============================================================================
// Instruction Templates — Move
// =============================================================================

inline constexpr uint32_t kMovRR   = 0xAA0003E0; // orr Xd, XZR, Xm (mov alias)
inline constexpr uint32_t kMovZ    = 0xD2800000; // movz Xd, #imm16
inline constexpr uint32_t kMovK16  = 0xF2A00000; // movk Xd, #imm16, lsl #16
inline constexpr uint32_t kMovK32  = 0xF2C00000; // movk Xd, #imm16, lsl #32
inline constexpr uint32_t kMovK48  = 0xF2E00000; // movk Xd, #imm16, lsl #48

// =============================================================================
// Instruction Templates — Bitfield (Shift Aliases)
// =============================================================================

inline constexpr uint32_t kUbfm = 0xD3400000; // ubfm Xd, Xn, #immr, #imms (for LSL/LSR)
inline constexpr uint32_t kSbfm = 0x93400000; // sbfm Xd, Xn, #immr, #imms (for ASR)

// =============================================================================
// Instruction Templates — Conditional
// =============================================================================

inline constexpr uint32_t kCset = 0x9A9F07E0; // csinc Xd, XZR, XZR, invert(cond)
inline constexpr uint32_t kCsel = 0x9A800000; // csel Xd, Xn, Xm, cond

// =============================================================================
// Instruction Templates — Load/Store Unsigned Offset
// =============================================================================

inline constexpr uint32_t kLdrGpr = 0xF9400000; // ldr Xt, [Xn, #imm12*8]
inline constexpr uint32_t kStrGpr = 0xF9000000; // str Xt, [Xn, #imm12*8]
inline constexpr uint32_t kLdrFpr = 0xFD400000; // ldr Dt, [Xn, #imm12*8]
inline constexpr uint32_t kStrFpr = 0xFD000000; // str Dt, [Xn, #imm12*8]

// Unscaled variants (for offsets not divisible by 8 or negative)
inline constexpr uint32_t kLdurGpr = 0xF8400000; // ldur Xt, [Xn, #simm9]
inline constexpr uint32_t kSturGpr = 0xF8000000; // stur Xt, [Xn, #simm9]
inline constexpr uint32_t kLdurFpr = 0xFC400000; // ldur Dt, [Xn, #simm9]
inline constexpr uint32_t kSturFpr = 0xFC000000; // stur Dt, [Xn, #simm9]

// =============================================================================
// Instruction Templates — Load/Store Pair
// =============================================================================

inline constexpr uint32_t kLdpGpr = 0xA9400000; // ldp Xt1, Xt2, [Xn, #imm7*8]
inline constexpr uint32_t kStpGpr = 0xA9000000; // stp Xt1, Xt2, [Xn, #imm7*8]
inline constexpr uint32_t kLdpFpr = 0x6D400000; // ldp Dt1, Dt2, [Xn, #imm7*8]
inline constexpr uint32_t kStpFpr = 0x6D000000; // stp Dt1, Dt2, [Xn, #imm7*8]

// Pre-indexed pair (for prologue: stp x29, x30, [sp, #-16]!)
inline constexpr uint32_t kStpGprPre = 0xA9800000;
inline constexpr uint32_t kLdpGprPost = 0xA8C00000; // ldp Xt1, Xt2, [Xn], #imm
inline constexpr uint32_t kStpFprPre = 0x6D800000;
inline constexpr uint32_t kLdpFprPost = 0x6CC00000;

// Single register pre/post-indexed (for odd callee-saved count)
inline constexpr uint32_t kStrGprPre  = 0xF8000C00; // str Xt, [Xn, #simm9]!
inline constexpr uint32_t kLdrGprPost = 0xF8400400; // ldr Xt, [Xn], #simm9
inline constexpr uint32_t kStrFprPre  = 0xFC000C00; // str Dt, [Xn, #simm9]!
inline constexpr uint32_t kLdrFprPost = 0xFC400400; // ldr Dt, [Xn], #simm9

// =============================================================================
// Instruction Templates — Floating Point
// =============================================================================

inline constexpr uint32_t kFAddRRR = 0x1E602800; // fadd Dd, Dn, Dm
inline constexpr uint32_t kFSubRRR = 0x1E603800; // fsub Dd, Dn, Dm
inline constexpr uint32_t kFMulRRR = 0x1E600800; // fmul Dd, Dn, Dm
inline constexpr uint32_t kFDivRRR = 0x1E601800; // fdiv Dd, Dn, Dm
inline constexpr uint32_t kFCmpRR  = 0x1E602000; // fcmp Dn, Dm
inline constexpr uint32_t kFMovRR  = 0x1E604000; // fmov Dd, Dn
inline constexpr uint32_t kFRintN  = 0x1E644000; // frintn Dd, Dn

// =============================================================================
// Instruction Templates — Int↔Float Conversion
// =============================================================================

inline constexpr uint32_t kSCvtF  = 0x9E620000; // scvtf Dd, Xn
inline constexpr uint32_t kFCvtZS = 0x9E780000; // fcvtzs Xd, Dn
inline constexpr uint32_t kUCvtF  = 0x9E630000; // ucvtf Dd, Xn
inline constexpr uint32_t kFCvtZU = 0x9E790000; // fcvtzu Xd, Dn
inline constexpr uint32_t kFMovGR = 0x9E670000; // fmov Dd, Xn (GPR→FPR bit transfer)

// =============================================================================
// Instruction Templates — Branch
// =============================================================================

inline constexpr uint32_t kBr   = 0x14000000; // b   label
inline constexpr uint32_t kBl   = 0x94000000; // bl  label
inline constexpr uint32_t kBCond = 0x54000000; // b.cond label
inline constexpr uint32_t kCbz  = 0xB4000000; // cbz Xt, label
inline constexpr uint32_t kCbnz = 0xB5000000; // cbnz Xt, label
inline constexpr uint32_t kBlr  = 0xD63F0000; // blr Xn
inline constexpr uint32_t kRet  = 0xD65F03C0; // ret x30

// =============================================================================
// Instruction Templates — Address Materialization
// =============================================================================

inline constexpr uint32_t kAdrp = 0x90000000; // adrp Xd, label

// =============================================================================
// Encoding Helpers
// =============================================================================

/// Build a 3-register instruction: template | (Rm << 16) | (Rn << 5) | Rd
constexpr uint32_t encode3Reg(uint32_t tmpl, uint32_t rd, uint32_t rn, uint32_t rm)
{
    return tmpl | (rm << 16) | (rn << 5) | rd;
}

/// Build a 4-register instruction (madd/msub): template | (Rm << 16) | (Ra << 10) | (Rn << 5) | Rd
constexpr uint32_t encode4Reg(uint32_t tmpl, uint32_t rd, uint32_t rn, uint32_t rm, uint32_t ra)
{
    return tmpl | (rm << 16) | (ra << 10) | (rn << 5) | rd;
}

/// Build an add/sub immediate instruction: template | (imm12 << 10) | (Rn << 5) | Rd
constexpr uint32_t encodeAddSubImm(uint32_t tmpl, uint32_t rd, uint32_t rn, uint32_t imm12)
{
    return tmpl | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

/// Build a 2-register instruction: template | (Rn << 5) | Rd
constexpr uint32_t encode2Reg(uint32_t tmpl, uint32_t rd, uint32_t rn)
{
    return tmpl | (rn << 5) | rd;
}

/// Build a load/store pair: template | ((imm7 & 0x7F) << 15) | (Rt2 << 10) | (Rn << 5) | Rt
constexpr uint32_t encodePair(uint32_t tmpl, uint32_t rt, uint32_t rt2, uint32_t rn, int32_t imm7)
{
    return tmpl | ((static_cast<uint32_t>(imm7) & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt;
}

} // namespace viper::codegen::aarch64::binenc
