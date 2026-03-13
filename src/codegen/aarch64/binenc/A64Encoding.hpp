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
// Logical immediate: AND/ORR/EOR Xd, Xn, #bitmask  (N:immr:imms encoding)
inline constexpr uint32_t kAndImm  = 0x92000000; // and  Xd, Xn, #bitmask
inline constexpr uint32_t kOrrImm  = 0xB2000000; // orr  Xd, Xn, #bitmask
inline constexpr uint32_t kEorImm  = 0xD2000000; // eor  Xd, Xn, #bitmask
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
inline constexpr uint32_t kMovZ    = 0xD2800000; // movz Xd, #imm16          (lsl #0)
inline constexpr uint32_t kMovZ16  = 0xD2A00000; // movz Xd, #imm16, lsl #16
inline constexpr uint32_t kMovZ32  = 0xD2C00000; // movz Xd, #imm16, lsl #32
inline constexpr uint32_t kMovZ48  = 0xD2E00000; // movz Xd, #imm16, lsl #48
inline constexpr uint32_t kMovN    = 0x92800000; // movn Xd, #imm16          (lsl #0)
inline constexpr uint32_t kMovN16  = 0x92A00000; // movn Xd, #imm16, lsl #16
inline constexpr uint32_t kMovN32  = 0x92C00000; // movn Xd, #imm16, lsl #32
inline constexpr uint32_t kMovN48  = 0x92E00000; // movn Xd, #imm16, lsl #48
inline constexpr uint32_t kMovK    = 0xF2800000; // movk Xd, #imm16          (lsl #0)
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

/// Build an add/sub immediate with shift: bit 22 selects lsl #12.
constexpr uint32_t encodeAddSubImmShift(uint32_t tmpl, uint32_t rd, uint32_t rn, uint32_t imm12)
{
    return tmpl | (1U << 22) | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

/// Build a 2-register instruction: template | (Rn << 5) | Rd
constexpr uint32_t encode2Reg(uint32_t tmpl, uint32_t rd, uint32_t rn)
{
    return tmpl | (rn << 5) | rd;
}

// =============================================================================
// Logical Immediate Encoding
// =============================================================================

/// Encode a 64-bit bitmask as a 13-bit AArch64 logical immediate (N:immr:imms).
/// Returns the 13-bit encoding on success, or -1 if the value is not encodable.
/// The algorithm finds the smallest repeating element size whose rotation produces
/// the input value. See ARM ARM section C6.2.
inline int32_t encodeLogicalImmediate(uint64_t val)
{
    if (val == 0 || val == ~0ULL)
        return -1; // All-zeros and all-ones are not encodable.

    // For each element size, check if the value is a rotated run of ones.
    for (unsigned size = 2; size <= 64; size <<= 1)
    {
        uint64_t mask = (~0ULL) >> (64 - size);
        uint64_t elem = val & mask;

        // Verify the value is a repeating pattern of this element.
        bool repeating = true;
        for (unsigned shift = size; shift < 64; shift += size)
        {
            if (((val >> shift) & mask) != elem)
            {
                repeating = false;
                break;
            }
        }
        if (!repeating)
            continue;

        // Try to decompose `elem` as a rotated run of ones within `size` bits.
        // Rotate elem to find the run length.
        // Double the element to handle wrap-around rotations.
        uint64_t doubled = elem | (elem << size);
        // Find a contiguous run of ones in `doubled`.
        // Count trailing zeros to find the start of a run.
        unsigned tz = 0;
        {
            uint64_t tmp = doubled;
            while (tz < 2 * size && (tmp & 1) == 0)
            {
                ++tz;
                tmp >>= 1;
            }
        }
        // Count the length of the run.
        unsigned runLen = 0;
        {
            uint64_t tmp = doubled >> tz;
            while (runLen < 2 * size && (tmp & 1) == 1)
            {
                ++runLen;
                tmp >>= 1;
            }
        }
        // After the run, the remaining bits must all be zero (within the doubled element).
        // Verify: (doubled >> tz >> runLen) has no more 1-bits within 2*size.
        {
            uint64_t remaining = doubled >> (tz + runLen);
            uint64_t checkMask = (size < 32) ? ((1ULL << size) - 1) : (size == 32 ? 0xFFFFFFFF : ~0ULL);
            // We need to check bits [tz+runLen .. 2*size-1]. But since we doubled,
            // if the run doesn't wrap cleanly, this element isn't valid.
            if (runLen == 0 || runLen >= size)
                continue; // No valid run found.
            // The run of `runLen` ones starting at bit `tz` within `size`-bit element.
            // rotation = tz mod size (where the run starts, measuring from bit 0).
            unsigned rotation = tz % size;
            // Verify by reconstructing.
            uint64_t reconstructed = 0;
            for (unsigned b = 0; b < runLen; ++b)
                reconstructed |= 1ULL << ((rotation + b) % size);
            if (reconstructed != elem)
                continue;
        }

        // Encode: immr = rotation within size, imms encodes (run_length - 1) and size.
        unsigned rotation = tz % size;
        unsigned immr = (size - rotation) % size; // Right-rotation amount.
        unsigned imms;
        unsigned N;
        if (size == 64)
        {
            N = 1;
            imms = runLen - 1;
        }
        else
        {
            N = 0;
            // imms has a size-encoding prefix: for size=32, top bit is 0; for 16, top 2 are 10; etc.
            // The pattern is: imms = (NOT(size-1) & 0x3F) | (runLen - 1)
            // More precisely: the high bits of imms encode the element size:
            //   size=2:  imms[5:1] = 11110x  (0x3C | (runLen-1))
            //   size=4:  imms[5:2] = 1110xx  (0x38 | (runLen-1))
            //   size=8:  imms[5:3] = 110xxx  (0x30 | (runLen-1))
            //   size=16: imms[5:4] = 10xxxx  (0x20 | (runLen-1))
            //   size=32: imms[5]   = 0xxxxx  (0x00 | (runLen-1))
            // Formula: ~(size*2 - 1) & 0x3F gives the prefix.
            unsigned prefix = (~(size * 2 - 1)) & 0x3F;
            imms = prefix | (runLen - 1);
        }

        return static_cast<int32_t>((N << 12) | (immr << 6) | imms);
    }
    return -1;
}

/// Build a logical immediate instruction: template | (N:immr:imms << 10) | (Rn << 5) | Rd
inline uint32_t encodeLogImm(uint32_t tmpl, uint32_t rd, uint32_t rn, int32_t nimms)
{
    // nimms = N:immr:imms (13 bits)
    return tmpl | (static_cast<uint32_t>(nimms) << 10) | (rn << 5) | rd;
}

// =============================================================================
// FP8 Immediate Encoding
// =============================================================================

/// FMOV Dd, #imm8 instruction template.
inline constexpr uint32_t kFMovDImm = 0x1E601000;

/// Encode a double-precision FP value as an 8-bit AArch64 FP immediate.
/// Returns the 8-bit encoding on success, or -1 if not encodable.
/// Encodable values: ±(1 + n/16) * 2^r where n ∈ [0,15], r ∈ [-3,4].
inline int32_t encodeFP8Immediate(double val)
{
    uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));

    uint32_t sign = static_cast<uint32_t>(bits >> 63) & 1;
    int32_t exp = static_cast<int32_t>((bits >> 52) & 0x7FF) - 1023;
    uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;

    // Exponent must be in [-3, 4].
    if (exp < -3 || exp > 4)
        return -1;

    // Low 48 mantissa bits must be zero (only 4 fractional bits allowed).
    if (mantissa & 0x0000FFFFFFFFFFFFULL)
        return -1;

    uint32_t frac4 = static_cast<uint32_t>((mantissa >> 48) & 0xF);

    // Encode exponent: value = NOT(b)*4 + c*2 + d - 3, where bcd are the 3 exponent bits.
    int32_t e = exp + 3; // Maps [-3,4] → [0,7].
    uint32_t b, cd;
    if (e >= 4)
    {
        b = 0;
        cd = static_cast<uint32_t>(e - 4);
    }
    else
    {
        b = 1;
        cd = static_cast<uint32_t>(e);
    }

    return static_cast<int32_t>((sign << 7) | (b << 6) | (cd << 4) | frac4);
}

/// Build a load/store pair: template | ((imm7 & 0x7F) << 15) | (Rt2 << 10) | (Rn << 5) | Rt
constexpr uint32_t encodePair(uint32_t tmpl, uint32_t rt, uint32_t rt2, uint32_t rn, int32_t imm7)
{
    return tmpl | ((static_cast<uint32_t>(imm7) & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt;
}

} // namespace viper::codegen::aarch64::binenc
