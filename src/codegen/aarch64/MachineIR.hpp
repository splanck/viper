// src/codegen/aarch64/MachineIR.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Minimal AArch64 Machine IR scaffolding (Phase A) used by tests and
//          early integration. Represents a tiny subset of instructions sufficient
//          to cover current CLI patterns: moves, add/sub/mul (rrr/ri), shifts-by-imm,
//          compares (rr/ri) with cset, and function/block containers.
// Invariants: MIR objects are simple POD-like containers; no ownership beyond
//             standard containers. Opcodes here are intentionally lean.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

enum class MOpcode
{
    MovRR,
    MovRI,
    // Stack pointer adjust (for outgoing arg area)
    SubSpImm,
    AddSpImm,
    // Store to outgoing arg area at [sp, #imm]
    StrRegSpImm,
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
    Cset, // dst, cond(code)
    Br,      // b label
    BCond,   // b.<cond> label
    Bl,      // bl <label> (call)
};

struct MOperand
{
    enum class Kind { Reg, Imm, Cond, Label } kind{Kind::Imm};
    PhysReg reg{PhysReg::X0};
    long long imm{0};
    const char *cond{nullptr};
    std::string label;

    static MOperand regOp(PhysReg r) { MOperand o{}; o.kind = Kind::Reg; o.reg = r; return o; }
    static MOperand immOp(long long v) { MOperand o{}; o.kind = Kind::Imm; o.imm = v; return o; }
    static MOperand condOp(const char *c) { MOperand o{}; o.kind = Kind::Cond; o.cond = c; return o; }
    static MOperand labelOp(std::string name) { MOperand o{}; o.kind = Kind::Label; o.label = std::move(name); return o; }
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
};

} // namespace viper::codegen::aarch64
