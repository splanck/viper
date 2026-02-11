//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/Peephole.hpp
// Purpose: Table-driven peephole optimisation pass -- matches instruction
//          patterns (constant operand, same-operands) and replaces them with
//          simpler equivalents (operand forwarding or literal synthesis).
//          Covers integer/float arithmetic identities, bitwise identities,
//          reflexive comparisons, and division/remainder simplifications.
// Key invariants:
//   - kRules is a compile-time constexpr array; adding rules does not require
//     modifying the pass engine.
//   - SSA form and value uses are maintained after replacement.
// Ownership/Lifetime: Free function operating on a caller-owned Module.
//          Rule table is static constexpr.
// Links: il/core/Opcode.hpp, il/core/fwd.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include "il/core/Opcode.hpp"
#include "il/core/fwd.hpp"

namespace il::transform
{

/// \brief Pattern describing when a rule should trigger.
struct Match
{
    /// Kinds of patterns the peephole engine supports.
    enum class Kind
    {
        ConstOperand,      ///< Match a specific integer constant at operand index.
        ConstFloatOperand, ///< Match a specific float constant at operand index.
        SameOperands       ///< Match when both operands are identical.
    };

    /// Opcode to match.
    core::Opcode op;
    /// Which matching strategy to use.
    Kind kind;
    /// Operand index holding the constant (ConstOperand/ConstFloatOperand) or unused
    /// (SameOperands).
    unsigned constIdx = 0;
    /// Required constant value (ConstOperand) - integer.
    long long value = 0;
    /// Required constant value (ConstFloatOperand) - float.
    double floatValue = 0.0;
};

/// \brief Replacement describing how to rewrite a matched instruction.
struct Replace
{
    /// Strategy for producing the replacement value.
    enum class Kind
    {
        Operand,   ///< Forward an existing operand.
        Const,     ///< Synthesize an integer/boolean literal.
        ConstFloat ///< Synthesize a floating-point literal.
    };

    Kind kind;
    /// Index of the operand to use when kind == Operand.
    unsigned operandIdx = 0;
    /// Constant payload when kind == Const.
    long long constValue = 0;
    /// Float constant payload when kind == ConstFloat.
    double floatConstValue = 0.0;
    /// Whether the constant literal is a boolean (i1) rather than i64.
    bool isBool = false;
};

/// \brief A peephole rule mapping a match to its replacement.
struct Rule
{
    Match match;      ///< Match pattern.
    Replace repl;     ///< Replacement action.
    const char *name; ///< Debug identifier for tracing.
};

/// \brief Registry of peephole rules.
inline constexpr std::array<Rule, 57> kRules{{
    // Integer arithmetic identities (checked overflow variants)
    {{core::Opcode::IAddOvf, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Operand, 1, 0, false},
     "iadd.ovf+x0"},
    {{core::Opcode::IAddOvf, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "iadd.ovf+0x"},
    {{core::Opcode::ISubOvf, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "isub.ovf-x0"},
    {{core::Opcode::IMulOvf, Match::Kind::ConstOperand, 0, 1},
     {Replace::Kind::Operand, 1, 0, false},
     "imul.ovf*1x"},
    {{core::Opcode::IMulOvf, Match::Kind::ConstOperand, 1, 1},
     {Replace::Kind::Operand, 0, 0, false},
     "imul.ovf*x1"},
    {{core::Opcode::IMulOvf, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "imul.ovf*0x"},
    {{core::Opcode::IMulOvf, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Const, 0, 0, false},
     "imul.ovf*x0"},
    {{core::Opcode::ISubOvf, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "isub.ovf-xx"},

    // Bitwise identities
    {{core::Opcode::And, Match::Kind::ConstOperand, 0, -1},
     {Replace::Kind::Operand, 1, 0, false},
     "and-1x"},
    {{core::Opcode::And, Match::Kind::ConstOperand, 1, -1},
     {Replace::Kind::Operand, 0, 0, false},
     "andx-1"},
    {{core::Opcode::And, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "and0x"},
    {{core::Opcode::And, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Const, 0, 0, false},
     "andx0"},
    {{core::Opcode::And, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "andxx"},
    {{core::Opcode::Or, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Operand, 1, 0, false},
     "or0x"},
    {{core::Opcode::Or, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "orx0"},
    {{core::Opcode::Or, Match::Kind::ConstOperand, 0, -1},
     {Replace::Kind::Const, 0, -1, false},
     "or-1x"},
    {{core::Opcode::Or, Match::Kind::ConstOperand, 1, -1},
     {Replace::Kind::Const, 0, -1, false},
     "orx-1"},
    {{core::Opcode::Or, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "orxx"},
    {{core::Opcode::Xor, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Operand, 1, 0, false},
     "xor0x"},
    {{core::Opcode::Xor, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "xorx0"},
    {{core::Opcode::Xor, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "xorxx"},

    // Shift identities
    {{core::Opcode::Shl, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "shl0"},
    {{core::Opcode::LShr, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "lshr0"},
    {{core::Opcode::AShr, Match::Kind::ConstOperand, 1, 0},
     {Replace::Kind::Operand, 0, 0, false},
     "ashr0"},
    {{core::Opcode::Shl, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "0shl"},
    {{core::Opcode::LShr, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "0lshr"},
    {{core::Opcode::AShr, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0, false},
     "0ashr"},

    // Reflexive comparisons
    {{core::Opcode::ICmpEq, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "icmp.eq-xx"},
    {{core::Opcode::ICmpNe, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "icmp.ne-xx"},
    {{core::Opcode::SCmpLT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "scmp.lt-xx"},
    {{core::Opcode::SCmpLE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "scmp.le-xx"},
    {{core::Opcode::SCmpGT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "scmp.gt-xx"},
    {{core::Opcode::SCmpGE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "scmp.ge-xx"},

    // Unsigned reflexive comparisons
    {{core::Opcode::UCmpLT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "ucmp.lt-xx"},
    {{core::Opcode::UCmpLE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "ucmp.le-xx"},
    {{core::Opcode::UCmpGT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "ucmp.gt-xx"},
    {{core::Opcode::UCmpGE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "ucmp.ge-xx"},

    // Float reflexive comparisons
    {{core::Opcode::FCmpEQ, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "fcmp.eq-xx"},
    {{core::Opcode::FCmpNE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "fcmp.ne-xx"},
    {{core::Opcode::FCmpLT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "fcmp.lt-xx"},
    {{core::Opcode::FCmpLE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "fcmp.le-xx"},
    {{core::Opcode::FCmpGT, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 0, 0.0, true},
     "fcmp.gt-xx"},
    {{core::Opcode::FCmpGE, Match::Kind::SameOperands, 0, 0},
     {Replace::Kind::Const, 0, 1, 0.0, true},
     "fcmp.ge-xx"},

    // Float arithmetic identities: x * 1.0 = x
    {{core::Opcode::FMul, Match::Kind::ConstFloatOperand, 0, 0, 1.0},
     {Replace::Kind::Operand, 1},
     "fmul*1x"},
    {{core::Opcode::FMul, Match::Kind::ConstFloatOperand, 1, 0, 1.0},
     {Replace::Kind::Operand, 0},
     "fmul*x1"},

    // Float arithmetic identities: x / 1.0 = x
    {{core::Opcode::FDiv, Match::Kind::ConstFloatOperand, 1, 0, 1.0},
     {Replace::Kind::Operand, 0},
     "fdiv/x1"},

    // Float arithmetic identities: x + 0.0 = x
    {{core::Opcode::FAdd, Match::Kind::ConstFloatOperand, 0, 0, 0.0},
     {Replace::Kind::Operand, 1},
     "fadd+0x"},
    {{core::Opcode::FAdd, Match::Kind::ConstFloatOperand, 1, 0, 0.0},
     {Replace::Kind::Operand, 0},
     "fadd+x0"},

    // Float arithmetic identities: x - 0.0 = x
    {{core::Opcode::FSub, Match::Kind::ConstFloatOperand, 1, 0, 0.0},
     {Replace::Kind::Operand, 0},
     "fsub-x0"},

    // Float arithmetic identities: x * 0.0 = 0.0 (note: not valid for NaN/Inf)
    // Skipped for safety - FP semantics require caution

    // Float arithmetic identities: x - x = 0.0 (note: not valid for NaN)
    // Skipped for safety - FP semantics require caution

    // Integer division identities: x / 1 = x
    {{core::Opcode::SDivChk0, Match::Kind::ConstOperand, 1, 1},
     {Replace::Kind::Operand, 0},
     "sdiv/x1"},
    {{core::Opcode::UDivChk0, Match::Kind::ConstOperand, 1, 1},
     {Replace::Kind::Operand, 0},
     "udiv/x1"},

    // Integer remainder identities: x % 1 = 0
    {{core::Opcode::SRemChk0, Match::Kind::ConstOperand, 1, 1},
     {Replace::Kind::Const, 0, 0},
     "srem%x1"},
    {{core::Opcode::URemChk0, Match::Kind::ConstOperand, 1, 1},
     {Replace::Kind::Const, 0, 0},
     "urem%x1"},

    // Integer division identities: 0 / x = 0 (x != 0 checked by Chk0)
    {{core::Opcode::SDivChk0, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0},
     "sdiv/0x"},
    {{core::Opcode::UDivChk0, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0},
     "udiv/0x"},

    // Integer remainder identities: 0 % x = 0 (x != 0 checked by Chk0)
    {{core::Opcode::SRemChk0, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0},
     "srem%0x"},
    {{core::Opcode::URemChk0, Match::Kind::ConstOperand, 0, 0},
     {Replace::Kind::Const, 0, 0},
     "urem%0x"},
}};

/// \brief Run peephole simplifications over @p m using registered rules.
void peephole(core::Module &m);

} // namespace il::transform
