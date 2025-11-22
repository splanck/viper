//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/MachineIR.hpp
// Purpose: Minimal AArch64 Machine IR scaffolding (Phase A) used by tests and 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cassert>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

enum class MOpcode
{
    MovRR,
    MovRI,
    // Floating-point (64-bit, scalar)
    FMovRR,
    FMovRI,
    FAddRRR,
    FSubRRR,
    FMulRRR,
    FDivRRR,
    FCmpRR,
    // Integer<->Float conversions (64-bit)
    SCvtF,   // scvtf dDst, xSrc
    FCvtZS,  // fcvtzs xDst, dSrc
    // Stack pointer adjust (for outgoing arg area)
    SubSpImm,
    AddSpImm,
    // Store to outgoing arg area at [sp, #imm]
    StrRegSpImm,
    StrFprSpImm,
    // Load/store from frame pointer (for locals)
    LdrRegFpImm, // dst, offset - ldr xN, [x29, #offset]
    StrRegFpImm, // src, offset - str xN, [x29, #offset]
    LdrFprFpImm, // dst(FPR), offset - ldr dN, [x29, #offset]
    StrFprFpImm, // src(FPR), offset - str dN, [x29, #offset]
    AddRRR,
    SubRRR,
    MulRRR,
    AndRRR,
    OrrRRR,
    EorRRR,
    AddRI,
    SubRI,
    LslRI,
    LsrRI,
    AsrRI,
    CmpRR,
    CmpRI,
    Cset,  // dst, cond(code)
    Br,    // b label
    BCond, // b.<cond> label
    Bl,    // bl <label> (call)
    // Address materialisation for globals (Mach-O style)
    AdrPage,    // dst, label  => adrp dst, label@PAGE
    AddPageOff, // dst, base, label => add dst, base, label@PAGEOFF
};

struct MReg
{
    bool isPhys{false};
    RegClass cls{RegClass::GPR};
    uint16_t idOrPhys{0U};
};

struct MOperand
{
    enum class Kind
    {
        Reg,
        Imm,
        Cond,
        Label
    } kind{Kind::Imm};
    MReg reg{};
    long long imm{0};
    const char *cond{nullptr};
    std::string label;

    static MOperand regOp(PhysReg r)
    {
        MOperand o{};
        o.kind = Kind::Reg;
        o.reg.isPhys = true;
        o.reg.cls = isGPR(r) ? RegClass::GPR : RegClass::FPR;
        o.reg.idOrPhys = static_cast<uint16_t>(r);
        return o;
    }

    static MOperand vregOp(RegClass cls, uint16_t id)
    {
        MOperand o{};
        o.kind = Kind::Reg;
        o.reg.isPhys = false;
        o.reg.cls = cls;
        o.reg.idOrPhys = id;
        return o;
    }

    static MOperand immOp(long long v)
    {
        MOperand o{};
        o.kind = Kind::Imm;
        o.imm = v;
        return o;
    }

    static MOperand condOp(const char *c)
    {
        MOperand o{};
        o.kind = Kind::Cond;
        o.cond = c;
        return o;
    }

    static MOperand labelOp(std::string name)
    {
        MOperand o{};
        o.kind = Kind::Label;
        o.label = std::move(name);
        return o;
    }
};

struct MInstr
{
    MOpcode opc{};
    std::vector<MOperand> ops{}; // compact, small-vec later if needed
};

struct MBasicBlock
{
    std::string name;
    std::vector<MInstr> instrs;
};

struct MFunction
{
    std::string name;
    std::vector<MBasicBlock> blocks;
    // Optional: list of callee-saved GPRs to be saved/restored in prologue/epilogue.
    std::vector<PhysReg> savedGPRs;
    // Optional: list of callee-saved FPRs (D-registers) to save/restore.
    std::vector<PhysReg> savedFPRs;
    // Size of local frame (stack-allocated locals), in bytes, 16-byte aligned
    int localFrameSize{0};
    struct StackLocal { unsigned tempId{0}; int size{0}; int align{8}; int offset{0}; };
    struct SpillSlot { uint16_t vreg{0}; int size{8}; int align{8}; int offset{0}; };
    struct FrameLayout
    {
        std::vector<StackLocal> locals; // locals keyed by IL temp id
        std::vector<SpillSlot> spills;   // spills keyed by vreg id
        int totalBytes{0};               // total frame size (locals + spills + outgoing), aligned
        int maxOutgoingBytes{0};         // optional max outgoing arg area (bytes)

        // Helpers
        int getLocalOffset(unsigned tempId) const
        {
            for (const auto &L : locals)
                if (L.tempId == tempId)
                    return L.offset;
            return 0;
        }
        int getSpillOffset(uint16_t vreg) const
        {
            for (const auto &S : spills)
                if (S.vreg == vreg)
                    return S.offset;
            return 0;
        }
    } frame;
};

} // namespace viper::codegen::aarch64
