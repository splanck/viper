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
    AddRRR,
    SubRRR,
    MulRRR,
    AddRI,
    SubRI,
    LslRI,
    LsrRI,
    AsrRI,
    CmpRR,
    CmpRI,
    Cset, // dst, cond(code)
};

struct MOperand
{
    enum class Kind { Reg, Imm, Cond } kind{Kind::Imm};
    PhysReg reg{PhysReg::X0};
    long long imm{0};
    const char *cond{nullptr};

    static MOperand regOp(PhysReg r) { return MOperand{Kind::Reg, r, 0, nullptr}; }
    static MOperand immOp(long long v) { return MOperand{Kind::Imm, PhysReg::X0, v, nullptr}; }
    static MOperand condOp(const char *c) { return MOperand{Kind::Cond, PhysReg::X0, 0, c}; }
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
};

} // namespace viper::codegen::aarch64

