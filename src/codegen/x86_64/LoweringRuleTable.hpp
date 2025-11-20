// src/codegen/x86_64/LoweringRuleTable.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Describe declarative lowering rules for x86-64 emission.
// Invariants: Rule tables are constexpr and reference stateless emitters.
// Ownership: Shared across lowering translation units via inline constexpr data.
// Notes: Tables are consumed to build matcher thunks inside LoweringRules.cpp.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace viper::codegen::x64
{

struct ILInstr;
class MIRBuilder;

namespace lowering
{

void emitAdd(const ILInstr &instr, MIRBuilder &builder);
void emitSub(const ILInstr &instr, MIRBuilder &builder);
void emitMul(const ILInstr &instr, MIRBuilder &builder);
void emitFDiv(const ILInstr &instr, MIRBuilder &builder);
void emitAnd(const ILInstr &instr, MIRBuilder &builder);
void emitOr(const ILInstr &instr, MIRBuilder &builder);
void emitXor(const ILInstr &instr, MIRBuilder &builder);
void emitICmp(const ILInstr &instr, MIRBuilder &builder);
void emitFCmp(const ILInstr &instr, MIRBuilder &builder);
void emitDivFamily(const ILInstr &instr, MIRBuilder &builder);
void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder);
void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder);
void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder);
void emitCmpExplicit(const ILInstr &instr, MIRBuilder &builder);
void emitSelect(const ILInstr &instr, MIRBuilder &builder);
void emitBranch(const ILInstr &instr, MIRBuilder &builder);
void emitCondBranch(const ILInstr &instr, MIRBuilder &builder);
void emitReturn(const ILInstr &instr, MIRBuilder &builder);
void emitCall(const ILInstr &instr, MIRBuilder &builder);
void emitCallIndirect(const ILInstr &instr, MIRBuilder &builder);
void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder);
void emitStore(const ILInstr &instr, MIRBuilder &builder);
void emitZSTrunc(const ILInstr &instr, MIRBuilder &builder);
void emitSIToFP(const ILInstr &instr, MIRBuilder &builder);
void emitFPToSI(const ILInstr &instr, MIRBuilder &builder);
void emitEhPush(const ILInstr &instr, MIRBuilder &builder);
void emitEhPop(const ILInstr &instr, MIRBuilder &builder);
void emitEhEntry(const ILInstr &instr, MIRBuilder &builder);

enum class RuleFlags : std::uint8_t
{
    None = 0,
    Prefix = 1U << 0,
};

constexpr RuleFlags operator|(RuleFlags lhs, RuleFlags rhs) noexcept
{
    return static_cast<RuleFlags>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr RuleFlags operator&(RuleFlags lhs, RuleFlags rhs) noexcept
{
    return static_cast<RuleFlags>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

constexpr bool hasFlag(RuleFlags flags, RuleFlags flag) noexcept
{
    return (flags & flag) != RuleFlags::None;
}

enum class OperandKindPattern : std::uint8_t
{
    Any,
    Value,
    Label,
    Immediate,
};

struct OperandShape
{
    std::uint8_t minArity{0};
    std::uint8_t maxArity{std::numeric_limits<std::uint8_t>::max()};
    std::uint8_t kindCount{0};
    std::array<OperandKindPattern, 4> kinds{OperandKindPattern::Any,
                                            OperandKindPattern::Any,
                                            OperandKindPattern::Any,
                                            OperandKindPattern::Any};
};

struct RuleSpec
{
    std::string_view opcode{};
    OperandShape operands{};
    RuleFlags flags{RuleFlags::None};
    void (*emit)(const ILInstr &, MIRBuilder &) = nullptr;
    const char *name{nullptr};
};

inline constexpr auto kLoweringRuleTable = std::array<RuleSpec, 35>{
    RuleSpec{"add",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitAdd,
             "add"},
    RuleSpec{"sub",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSub,
             "sub"},
    RuleSpec{"mul",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitMul,
             "mul"},
    RuleSpec{"fdiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitFDiv,
             "fdiv"},
    RuleSpec{"and",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitAnd,
             "and"},
    RuleSpec{"or",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitOr,
             "or"},
    RuleSpec{"xor",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitXor,
             "xor"},
    RuleSpec{"icmp_",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::Prefix,
             &emitICmp,
             "icmp"},
    RuleSpec{"fcmp_",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::Prefix,
             &emitFCmp,
             "fcmp"},
    RuleSpec{"div",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "div"},
    RuleSpec{"sdiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "sdiv"},
    RuleSpec{"srem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "srem"},
    RuleSpec{"udiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "udiv"},
    RuleSpec{"urem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "urem"},
    RuleSpec{"rem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "rem"},
    RuleSpec{"shl",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftLeft,
             "shl"},
    RuleSpec{"lshr",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftLshr,
             "lshr"},
    RuleSpec{"ashr",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftAshr,
             "ashr"},
    RuleSpec{"cmp",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCmpExplicit,
             "cmp"},
    RuleSpec{"select",
             OperandShape{3U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSelect,
             "select"},
    RuleSpec{"br",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitBranch,
             "br"},
    RuleSpec{"cbr",
             OperandShape{3U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Label,
                           OperandKindPattern::Label,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCondBranch,
             "cbr"},
    RuleSpec{"ret",
             OperandShape{0U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitReturn,
             "ret"},
    RuleSpec{"call",
             OperandShape{1U,
                          std::numeric_limits<std::uint8_t>::max(),
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCall,
             "call"},
    RuleSpec{"call.indirect",
             OperandShape{1U,
                          std::numeric_limits<std::uint8_t>::max(),
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCallIndirect,
             "call.indirect"},
    RuleSpec{"load",
             OperandShape{1U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitLoadAuto,
             "load"},
    RuleSpec{"store",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitStore,
             "store"},
    RuleSpec{"zext",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "zext"},
    RuleSpec{"sext",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "sext"},
    RuleSpec{"trunc",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "trunc"},
    RuleSpec{"sitofp",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSIToFP,
             "sitofp"},
    RuleSpec{"fptosi",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitFPToSI,
             "fptosi"},
    RuleSpec{"eh.push",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitEhPush,
             "eh.push"},
    RuleSpec{"eh.pop", OperandShape{0U, 0U, 0U, {}}, RuleFlags::None, &emitEhPop, "eh.pop"},
    RuleSpec{"eh.entry", OperandShape{0U, 0U, 0U, {}}, RuleFlags::None, &emitEhEntry, "eh.entry"},
};

} // namespace lowering

bool matchesRuleSpec(const lowering::RuleSpec &spec, const ILInstr &instr);
const lowering::RuleSpec *lookupRuleSpec(const ILInstr &instr);

} // namespace viper::codegen::x64
