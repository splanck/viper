//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/AsmEmitter.cpp
// Purpose: Materialise textual x86-64 assembly from Machine IR functions while
//          maintaining deterministic literal pools for read-only data.
// Key invariants: Emission preserves operand ordering, branch destinations, and
//                 condition suffixes carried by Machine IR. Literal pools
//                 deduplicate entries, emit stable labels, and are never emitted
//                 when empty.
// Ownership/Lifetime: AsmEmitter borrows the caller-provided RoDataPool; the
//                     pool outlives the emitter and continues to own all stored
//                     literal buffers.
// Links: docs/codemap.md#codegen, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "AsmEmitter.hpp"
#include "asmfmt/Format.hpp"
#include "common/Mangle.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace viper::codegen::x64
{

namespace
{

/// @brief Implements makePattern functionality.
/// @return Return value description needed.
constexpr OperandPattern makePattern(OperandKind first = OperandKind::None,
                                     OperandKind second = OperandKind::None,
                                     OperandKind third = OperandKind::None) noexcept
{
    OperandPattern pattern{};
    pattern.kinds[0] = first;
    pattern.kinds[1] = second;
    pattern.kinds[2] = third;
/// @brief Implements if functionality.
/// @param OperandKind::None Parameter description needed.
/// @return Return value description needed.
    if (first != OperandKind::None)
    {
        ++pattern.count;
    }
/// @brief Implements if functionality.
/// @param OperandKind::None Parameter description needed.
/// @return Return value description needed.
    if (second != OperandKind::None)
    {
        ++pattern.count;
    }
/// @brief Implements if functionality.
/// @param OperandKind::None Parameter description needed.
/// @return Return value description needed.
    if (third != OperandKind::None)
    {
        ++pattern.count;
    }
    return pattern;
}

/// @brief Implements function functionality.
/// @param lhs Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
constexpr EncodingFlag operator|(EncodingFlag lhs, EncodingFlag rhs) noexcept
{
    return static_cast<EncodingFlag>(static_cast<std::uint32_t>(lhs) |
                                     static_cast<std::uint32_t>(rhs));
}

/// @brief Checks if flag exists.
/// @param value Parameter description needed.
/// @param flag Parameter description needed.
/// @return Return value description needed.
[[maybe_unused]] constexpr bool hasFlag(EncodingFlag value, EncodingFlag flag) noexcept
{
/// @brief Implements return functionality.
/// @param static_cast<std::uint32_t>(value Parameter description needed.
/// @return Return value description needed.
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

static constexpr std::array<EncodingRow, 44> kEncodingTable = {{
    {MOpcode::MOVrr,
     "movq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::MOVri,
     "movq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::UsesImm64},
    {MOpcode::CMOVNErr,
     "cmovne",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::LEA,
     "leaq",
     EncodingForm::Lea,
     OperandOrder::LEA,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Any Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Any),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::ADDrr,
     "addq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::ADDri,
     "addq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm32 | EncodingFlag::REXW},
    {MOpcode::ANDrr,
     "andq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::ANDri,
     "andq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm32 | EncodingFlag::REXW},
    {MOpcode::ORrr,
     "orq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::ORri,
     "orq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm32 | EncodingFlag::REXW},
    {MOpcode::XORrr,
     "xorq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::XORri,
     "xorq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm32 | EncodingFlag::REXW},
    {MOpcode::SUBrr,
     "subq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::SHLri,
     "shlq",
     EncodingForm::ShiftImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm8 | EncodingFlag::REXW},
    {MOpcode::SHLrc,
     "shlq",
     EncodingForm::ShiftReg,
     OperandOrder::SHIFT,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::SHRri,
     "shrq",
     EncodingForm::ShiftImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm8 | EncodingFlag::REXW},
    {MOpcode::SHRrc,
     "shrq",
     EncodingForm::ShiftReg,
     OperandOrder::SHIFT,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::SARri,
     "sarq",
     EncodingForm::ShiftImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm8 | EncodingFlag::REXW},
    {MOpcode::SARrc,
     "sarq",
     EncodingForm::ShiftReg,
     OperandOrder::SHIFT,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::IMULrr,
     "imulq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::CQO,
     "cqto",
     EncodingForm::Nullary,
     OperandOrder::NONE,
/// @brief Implements makePattern functionality.
/// @return Return value description needed.
     makePattern(),
     EncodingFlag::REXW},
    {MOpcode::IDIVrm,
     "idivq",
     EncodingForm::Unary,
     OperandOrder::DIRECT,
/// @brief Implements makePattern functionality.
/// @param OperandKind::RegOrMem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::RegOrMem),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::DIVrm,
     "divq",
     EncodingForm::Unary,
     OperandOrder::DIRECT,
/// @brief Implements makePattern functionality.
/// @param OperandKind::RegOrMem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::RegOrMem),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::XORrr32,
     "xorl",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::CMPrr,
     "cmpq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::CMPri,
     "cmpq",
     EncodingForm::RegImm,
     OperandOrder::R_I,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Imm Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Imm),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesImm32 | EncodingFlag::REXW},
    {MOpcode::SETcc,
     "set",
     EncodingForm::Setcc,
     OperandOrder::SETCC,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Imm Parameter description needed.
/// @param OperandKind::RegOrMem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Imm, OperandKind::RegOrMem),
     EncodingFlag::RequiresModRM | EncodingFlag::UsesCondition},
    {MOpcode::MOVZXrr32,
     "movzbq",
     EncodingForm::RegReg,
     OperandOrder::MOVZX_RR8,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::TESTrr,
     "testq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::JMP,
     "jmp",
     EncodingForm::Jump,
     OperandOrder::JUMP,
/// @brief Implements makePattern functionality.
/// @param OperandKind::LabelOrRegOrMem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::LabelOrRegOrMem),
     EncodingFlag::None},
    {MOpcode::JCC,
     "j",
     EncodingForm::Condition,
     OperandOrder::JCC,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Imm Parameter description needed.
/// @param OperandKind::LabelOrRegOrMem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Imm, OperandKind::LabelOrRegOrMem),
     EncodingFlag::UsesCondition},
    {MOpcode::CALL,
     "callq",
     EncodingForm::Call,
     OperandOrder::CALL,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Any Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Any),
     EncodingFlag::None},
    {MOpcode::UD2,
     "ud2",
     EncodingForm::Nullary,
     OperandOrder::NONE,
/// @brief Implements makePattern functionality.
/// @return Return value description needed.
     makePattern(),
     EncodingFlag::None},
    {MOpcode::RET,
     "ret",
     EncodingForm::Nullary,
     OperandOrder::NONE,
/// @brief Implements makePattern functionality.
/// @return Return value description needed.
     makePattern(),
     EncodingFlag::None},
    {MOpcode::FADD,
     "addsd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::FSUB,
     "subsd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::FMUL,
     "mulsd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::FDIV,
     "divsd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::UCOMIS,
     "ucomisd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::CVTSI2SD,
     "cvtsi2sdq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::CVTTSD2SI,
     "cvttsd2siq",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM | EncodingFlag::REXW},
    {MOpcode::MOVSDrr,
     "movsd",
     EncodingForm::RegReg,
     OperandOrder::R_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
    {MOpcode::MOVSDrm,
     "movsd",
     EncodingForm::RegMem,
     OperandOrder::R_M,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Reg Parameter description needed.
/// @param OperandKind::Mem Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Reg, OperandKind::Mem),
     EncodingFlag::RequiresModRM},
    {MOpcode::MOVSDmr,
     "movsd",
     EncodingForm::MemReg,
     OperandOrder::M_R,
/// @brief Implements makePattern functionality.
/// @param OperandKind::Mem Parameter description needed.
/// @param OperandKind::Reg Parameter description needed.
/// @return Return value description needed.
     makePattern(OperandKind::Mem, OperandKind::Reg),
     EncodingFlag::RequiresModRM},
}};

/// @brief Implements matchesOperandKind functionality.
/// @param kind Parameter description needed.
/// @param operand Parameter description needed.
/// @return Return value description needed.
[[nodiscard]] bool matchesOperandKind(OperandKind kind, const Operand &operand) noexcept
{
/// @brief Implements switch functionality.
/// @param kind Parameter description needed.
/// @return Return value description needed.
    switch (kind)
    {
        case OperandKind::None:
            return false;
        case OperandKind::Reg:
            return std::holds_alternative<OpReg>(operand);
        case OperandKind::Imm:
            return std::holds_alternative<OpImm>(operand);
        case OperandKind::Mem:
            return std::holds_alternative<OpMem>(operand) ||
                   std::holds_alternative<OpRipLabel>(operand);
        case OperandKind::Label:
            return std::holds_alternative<OpLabel>(operand);
        case OperandKind::RipLabel:
            return std::holds_alternative<OpRipLabel>(operand);
        case OperandKind::RegOrMem:
            return std::holds_alternative<OpReg>(operand) || std::holds_alternative<OpMem>(operand);
        case OperandKind::RegOrImm:
            return std::holds_alternative<OpReg>(operand) || std::holds_alternative<OpImm>(operand);
        case OperandKind::LabelOrRegOrMem:
            return std::holds_alternative<OpLabel>(operand) ||
                   std::holds_alternative<OpRipLabel>(operand) ||
                   std::holds_alternative<OpReg>(operand) || std::holds_alternative<OpMem>(operand);
        case OperandKind::Any:
            return true;
    }
    return false;
}

/// @brief Implements matchesPattern functionality.
/// @return Return value description needed.
[[nodiscard]] bool matchesPattern(const OperandPattern &pattern,
                                  std::span<const Operand> operands) noexcept
{
/// @brief Implements if functionality.
/// @param static_cast<std::size_t>(pattern.count Parameter description needed.
/// @return Return value description needed.
    if (static_cast<std::size_t>(pattern.count) != operands.size())
    {
        return false;
    }
/// @brief Implements for functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < operands.size(); ++i)
    {
/// @brief Implements if functionality.
/// @param !matchesOperandKind(pattern.kinds[i] Parameter description needed.
/// @param operands[i] Parameter description needed.
/// @return Return value description needed.
        if (!matchesOperandKind(pattern.kinds[i], operands[i]))
        {
            return false;
        }
    }
    return true;
}

/// @brief Provide an overload set capable of visiting std::variant operands.
/// @details Aggregates multiple lambda visitors into a single callable so
///          @ref std::visit can dispatch over operand kinds without defining a
///          bespoke struct at each call site.
template <typename... Ts> struct Overload : Ts...
{
    using Ts::operator()...;
};

template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

enum : std::uint8_t
{
    kFmtDirect = 1U << 0U,
    kFmtShift = 1U << 1U,
    kFmtMovzx8 = 1U << 2U,
    kFmtLea = 1U << 3U,
    kFmtCall = 1U << 4U,
    kFmtJump = 1U << 5U,
    kFmtCond = 1U << 6U,
    kFmtSetcc = 1U << 7U,
};

struct OpFmt
{
    MOpcode opc;
    const char *mnemonic;
    std::uint8_t operandCount;
    std::uint8_t flags;
};

static constexpr std::array<OpFmt, 44> kOpFmt = {{
    {MOpcode::MOVrr, "movq", 2U, 0U},
    {MOpcode::CMOVNErr, "cmovne", 2U, 0U},
    {MOpcode::MOVri, "movq", 2U, 0U},
    {MOpcode::LEA, "leaq", 2U, kFmtLea},
    {MOpcode::ADDrr, "addq", 2U, 0U},
    {MOpcode::ADDri, "addq", 2U, 0U},
    {MOpcode::ANDrr, "andq", 2U, 0U},
    {MOpcode::ANDri, "andq", 2U, 0U},
    {MOpcode::ORrr, "orq", 2U, 0U},
    {MOpcode::ORri, "orq", 2U, 0U},
    {MOpcode::XORrr, "xorq", 2U, 0U},
    {MOpcode::XORri, "xorq", 2U, 0U},
    {MOpcode::SUBrr, "subq", 2U, 0U},
    {MOpcode::SHLri, "shlq", 2U, 0U},
    {MOpcode::SHLrc, "shlq", 2U, kFmtShift},
    {MOpcode::SHRri, "shrq", 2U, 0U},
    {MOpcode::SHRrc, "shrq", 2U, kFmtShift},
    {MOpcode::SARri, "sarq", 2U, 0U},
    {MOpcode::SARrc, "sarq", 2U, kFmtShift},
    {MOpcode::IMULrr, "imulq", 2U, 0U},
    {MOpcode::CQO, "cqto", 0U, 0U},
    {MOpcode::IDIVrm, "idivq", 1U, 0U},
    {MOpcode::DIVrm, "divq", 1U, 0U},
    {MOpcode::XORrr32, "xorl", 2U, 0U},
    {MOpcode::CMPrr, "cmpq", 2U, 0U},
    {MOpcode::CMPri, "cmpq", 2U, 0U},
    {MOpcode::SETcc, "set", 2U, static_cast<std::uint8_t>(kFmtCond | kFmtSetcc)},
    {MOpcode::MOVZXrr32, "movzbq", 2U, kFmtMovzx8},
    {MOpcode::TESTrr, "testq", 2U, 0U},
    {MOpcode::JMP, "jmp", 1U, kFmtJump},
    {MOpcode::JCC, "j", 2U, static_cast<std::uint8_t>(kFmtJump | kFmtCond)},
    {MOpcode::CALL, "callq", 1U, kFmtCall},
    {MOpcode::UD2, "ud2", 0U, 0U},
    {MOpcode::RET, "ret", 0U, 0U},
    {MOpcode::FADD, "addsd", 2U, 0U},
    {MOpcode::FSUB, "subsd", 2U, 0U},
    {MOpcode::FMUL, "mulsd", 2U, 0U},
    {MOpcode::FDIV, "divsd", 2U, 0U},
    {MOpcode::UCOMIS, "ucomisd", 2U, 0U},
    {MOpcode::CVTSI2SD, "cvtsi2sdq", 2U, 0U},
    {MOpcode::CVTTSD2SI, "cvttsd2siq", 2U, 0U},
    {MOpcode::MOVSDrr, "movsd", 2U, 0U},
    {MOpcode::MOVSDrm, "movsd", 2U, 0U},
    {MOpcode::MOVSDmr, "movsd", 2U, 0U},
}};

/// @brief Retrieves fmt value.
/// @param opc Parameter description needed.
/// @return Return value description needed.
const OpFmt *getFmt(MOpcode opc) noexcept
{
    const auto needle = static_cast<std::underlying_type_t<MOpcode>>(opc);
    const auto it =
        std::lower_bound(kOpFmt.begin(),
                         kOpFmt.end(),
                         needle,
                         [](const OpFmt &fmt, std::underlying_type_t<MOpcode> value)
                         { return static_cast<std::underlying_type_t<MOpcode>>(fmt.opc) < value; });
/// @brief Implements if functionality.
/// @param kOpFmt.end( Parameter description needed.
/// @return Return value description needed.
    if (it == kOpFmt.end())
    {
        return nullptr;
    }
/// @brief Implements if functionality.
/// @param static_cast<std::underlying_type_t<MOpcode>>(it->opc Parameter description needed.
/// @return Return value description needed.
    if (static_cast<std::underlying_type_t<MOpcode>>(it->opc) != needle)
    {
        return nullptr;
    }
    return &*it;
}

/// @brief Implements encodeRegister functionality.
/// @param reg Parameter description needed.
/// @return Return value description needed.
[[nodiscard]] int encodeRegister(const OpReg &reg) noexcept;

/// @brief Emits operand.
/// @param operand Parameter description needed.
/// @param out Parameter description needed.
/// @param target Parameter description needed.
template <typename Out> void emitOperand(const Operand &operand, Out &out, const TargetInfo &target)
{
    static_cast<void>(target);
    std::visit(Overload{[&](const OpReg &reg) { out << asmfmt::fmt_reg(encodeRegister(reg)); },
                        [&](const OpImm &imm) { out << asmfmt::format_imm(imm.val); },
                        [&](const OpMem &mem)
                        {
                            asmfmt::MemAddr addr{};
                            addr.base = encodeRegister(mem.base);
                            addr.disp = mem.disp;
                            out << asmfmt::format_mem(addr);
                        },
                        [&](const OpLabel &label) { out << asmfmt::format_label(label.name); },
                        [&](const OpRipLabel &label)
                        { out << asmfmt::format_rip_label(label.name); }},
               operand);
}

template <typename Out>
/// @brief Emits operands.
/// @param operands Parameter description needed.
/// @param out Parameter description needed.
/// @param target Parameter description needed.
void emitOperands(std::span<const Operand> operands, Out &out, const TargetInfo &target)
{
    bool first = true;
/// @brief Implements for functionality.
/// @param operands Parameter description needed.
/// @return Return value description needed.
    for (const auto &operand : operands)
    {
/// @brief Implements if functionality.
/// @param !first Parameter description needed.
/// @return Return value description needed.
        if (!first)
        {
            out << ", ";
        }
/// @brief Emits operand.
/// @param operand Parameter description needed.
/// @param out Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
        emitOperand(operand, out, target);
        first = false;
    }
}

/// @brief Emits rodatapool.
void emitRoDataPool(std::span<const std::string> stringLiterals,
                    std::span<const std::size_t> stringLengths,
                    std::span<const double> f64Literals,
                    std::ostream &os)
{
/// @brief Implements if functionality.
/// @param stringLiterals.empty( Parameter description needed.
/// @return Return value description needed.
    if (stringLiterals.empty() && f64Literals.empty())
    {
        return;
    }
/// @brief Implements assert functionality.
/// @param stringLiterals.size( Parameter description needed.
/// @return Return value description needed.
    assert(stringLiterals.size() == stringLengths.size());
    static_cast<void>(stringLengths);
    os << ".section .rodata\n";
/// @brief Implements for functionality.
/// @param stringLiterals.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < stringLiterals.size(); ++i)
    {
        std::string label;
        label.reserve(16U);
        label.append(".LC_str_");
        label.append(std::to_string(i));
        os << label << ":\n";
        os << asmfmt::format_rodata_bytes(stringLiterals[i]);
    }
/// @brief Implements if functionality.
/// @param !f64Literals.empty( Parameter description needed.
/// @return Return value description needed.
    if (!f64Literals.empty())
    {
        os << "  .p2align 3\n";
    }
/// @brief Implements for functionality.
/// @param f64Literals.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < f64Literals.size(); ++i)
    {
        std::string label;
        label.reserve(16U);
        label.append(".LC_f64_");
        label.append(std::to_string(i));
        os << label << ":\n";
        const auto bits = std::bit_cast<std::uint64_t>(f64Literals[i]);
        const auto oldFlags = os.flags();
        const auto oldFill = os.fill();
        os << "  .quad 0x" << std::hex << std::setw(16) << std::setfill('0') << bits << std::dec;
        os.fill(oldFill);
        os.flags(oldFlags);
        os << '\n';
    }
}

/// @brief Implements encodeRegister functionality.
/// @param reg Parameter description needed.
/// @return Return value description needed.
[[nodiscard]] int encodeRegister(const OpReg &reg) noexcept
{
/// @brief Implements if functionality.
/// @param reg.isPhys Parameter description needed.
/// @return Return value description needed.
    if (reg.isPhys)
    {
        return static_cast<int>(reg.idOrPhys);
    }
    return -static_cast<int>(reg.idOrPhys) - 1;
}

} // namespace

/// @brief Implements find_encoding functionality.
/// @param op Parameter description needed.
/// @param operands Parameter description needed.
/// @return Return value description needed.
const EncodingRow *find_encoding(MOpcode op, std::span<const Operand> operands) noexcept
{
/// @brief Implements for functionality.
/// @param kEncodingTable Parameter description needed.
/// @return Return value description needed.
    for (const auto &row : kEncodingTable)
    {
/// @brief Implements if functionality.
/// @param op Parameter description needed.
/// @return Return value description needed.
        if (row.opcode != op)
        {
            continue;
        }
/// @brief Implements if functionality.
/// @param matchesPattern(row.pattern Parameter description needed.
/// @param operands Parameter description needed.
/// @return Return value description needed.
        if (matchesPattern(row.pattern, operands))
        {
            return &row;
        }
    }
    return nullptr;
}

/// @brief Intern a string literal into the read-only data pool.
/// @details Deduplicates identical byte sequences so repeated literals emit a
///          single `.rodata` entry. New literals are appended to the pool and
///          assigned a stable numeric index.
/// @param bytes Literal payload to store.
/// @return Index referencing the canonicalised literal.
int AsmEmitter::RoDataPool::addStringLiteral(std::string bytes)
{
/// @brief Implements if functionality.
/// @param stringLookup_.find(bytes Parameter description needed.
/// @return Return value description needed.
    if (const auto it = stringLookup_.find(bytes); it != stringLookup_.end())
    {
        return it->second;
    }
    const int index = static_cast<int>(stringLiterals_.size());
    stringLookup_.emplace(bytes, index);
    stringLengths_.push_back(bytes.size());
    stringLiterals_.push_back(std::move(bytes));
    return index;
}

/// @brief Intern a 64-bit floating literal into the read-only data pool.
/// @details Bit-casts the floating value and deduplicates based on the
///          resulting bit pattern, ensuring `+0.0` and `-0.0` remain distinct.
/// @param value Floating-point literal to store.
/// @return Index referencing the canonical literal entry.
int AsmEmitter::RoDataPool::addF64Literal(double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
/// @brief Implements if functionality.
/// @param f64Lookup_.find(bits Parameter description needed.
/// @return Return value description needed.
    if (const auto it = f64Lookup_.find(bits); it != f64Lookup_.end())
    {
        return it->second;
    }
    const int index = static_cast<int>(f64Literals_.size());
    f64Lookup_.emplace(bits, index);
    f64Literals_.push_back(value);
    return index;
}

/// @brief Generate the assembly label for a stored string literal.
/// @param index Pool index returned by @ref addStringLiteral.
/// @return Mangled label suitable for use in assembly.
std::string AsmEmitter::RoDataPool::stringLabel(int index) const
{
    std::string label;
    label.reserve(16); // ".LC_str_" (9 chars) + digits + null terminator
    label = ".LC_str_";
    label += std::to_string(index);
    return label;
}

/// @brief Retrieve the byte length recorded for a string literal entry.
/// @param index Pool index supplied by @ref addStringLiteral.
/// @return Number of bytes stored for the literal.
std::size_t AsmEmitter::RoDataPool::stringByteLength(int index) const
{
/// @brief Implements assert functionality.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    assert(index >= 0);
    const auto idx = static_cast<std::size_t>(index);
/// @brief Implements assert functionality.
/// @param stringLengths_.size( Parameter description needed.
/// @return Return value description needed.
    assert(idx < stringLengths_.size());
    return stringLengths_[idx];
}

/// @brief Generate the assembly label for a stored 64-bit float literal.
/// @param index Pool index returned by @ref addF64Literal.
/// @return Mangled label suitable for use in assembly.
std::string AsmEmitter::RoDataPool::f64Label(int index) const
{
    std::string label;
    label.reserve(16); // ".LC_f64_" (9 chars) + digits + null terminator
    label = ".LC_f64_";
    label += std::to_string(index);
    return label;
}

/// @brief Emit the `.rodata` directives for all stored literals.
/// @details Writes a `.section .rodata` header followed by labels and
///          directives for each pooled string and floating literal. The method
///          preserves insertion order so indices map consistently to labels.
/// @param os Output stream receiving assembly text.
void AsmEmitter::RoDataPool::emit(std::ostream &os) const
{
/// @brief Implements if functionality.
/// @param empty( Parameter description needed.
/// @return Return value description needed.
    if (empty())
    {
        return;
    }
/// @brief Emits rodatapool.
/// @return Return value description needed.
    emitRoDataPool(std::span<const std::string>{stringLiterals_},
                   std::span<const std::size_t>{stringLengths_},
                   std::span<const double>{f64Literals_},
                   os);
}

/// @brief Query whether the pool currently holds any literals.
/// @return @c true when no string or floating literals have been interned.
bool AsmEmitter::RoDataPool::empty() const noexcept
{
    return stringLiterals_.empty() && f64Literals_.empty();
}

/// @brief Construct an emitter bound to a shared read-only data pool.
/// @param pool Pool responsible for owning literal buffers referenced by the
///             emitted assembly.
AsmEmitter::AsmEmitter(RoDataPool &pool) noexcept : pool_{&pool} {}

/// @brief Emit an assembly function, including basic blocks and instructions.
/// @details Writes the `.text` header, global symbol directive, function label,
///          and each Machine IR block. The first block is treated as the entry
///          and emitted without a label when it already matches the function
///          name.
/// @param os Output stream receiving the assembly.
/// @param func Machine IR function to serialise.
/// @param target Target lowering information controlling register selection.
void AsmEmitter::emitFunction(std::ostream &os,
                              const MFunction &func,
                              const TargetInfo &target) const
{
    os << ".text\n";
    const std::string linkName = viper::common::MangleLink(func.name);
    os << ".globl " << linkName << "\n";
    os << linkName << ":\n";

/// @brief Implements for functionality.
/// @param func.blocks.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < func.blocks.size(); ++i)
    {
        const auto &block = func.blocks[i];
        const bool isEntry = (i == 0U && block.label == func.name);
/// @brief Implements if functionality.
/// @param isEntry Parameter description needed.
/// @return Return value description needed.
        if (isEntry)
        {
/// @brief Implements for functionality.
/// @param block.instructions Parameter description needed.
/// @return Return value description needed.
            for (const auto &instr : block.instructions)
            {
/// @brief Emits instruction.
/// @param os Parameter description needed.
/// @param instr Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
                emitInstruction(os, instr, target);
            }
        }
        else
        {
/// @brief Emits block.
/// @param os Parameter description needed.
/// @param block Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitBlock(os, block, target);
        }
/// @brief Implements if functionality.
/// @param func.blocks.size( Parameter description needed.
/// @return Return value description needed.
        if (i + 1 < func.blocks.size())
        {
            os << '\n';
        }
    }
}

/// @brief Emit the `.rodata` section for literals referenced by emitted code.
/// @details Forwards to the shared pool only when it contains entries so that
///          translation units without literals avoid spurious section headers.
/// @param os Output stream receiving the assembly.
void AsmEmitter::emitRoData(std::ostream &os) const
{
/// @brief Implements if functionality.
/// @param !pool_->empty( Parameter description needed.
/// @return Return value description needed.
    if (pool_ && !pool_->empty())
    {
        pool_->emit(os);
    }
}

/// @brief Access the underlying literal pool.
/// @return Mutable reference to the associated pool.
AsmEmitter::RoDataPool &AsmEmitter::roDataPool() noexcept
{
    return *pool_;
}

/// @brief Access the underlying literal pool (const overload).
/// @return Const reference to the associated pool.
const AsmEmitter::RoDataPool &AsmEmitter::roDataPool() const noexcept
{
    return *pool_;
}

/// @brief Emit a labelled basic block and all contained instructions.
/// @details Prints the block label when present and delegates each instruction
///          to @ref emitInstruction.
/// @param os Output stream receiving the assembly.
/// @param block Machine basic block to serialise.
/// @param target Target lowering information controlling operand formatting.
void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &block, const TargetInfo &target)
{
/// @brief Implements if functionality.
/// @param !block.label.empty( Parameter description needed.
/// @return Return value description needed.
    if (!block.label.empty())
    {
        os << block.label << ":\n";
    }
/// @brief Implements for functionality.
/// @param block.instructions Parameter description needed.
/// @return Return value description needed.
    for (const auto &instr : block.instructions)
    {
/// @brief Emits instruction.
/// @param os Parameter description needed.
/// @param instr Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
        emitInstruction(os, instr, target);
    }
}

/// @brief Emit a single Machine IR instruction in AT&T syntax.
/// @details Handles opcode-specific quirks (such as operand ordering for
///          `MOV`, condition suffixes, and synthetic PX_COPY) before falling
///          back to a generic visitor that prints each operand.
/// @param os Output stream receiving the assembly.
/// @param instr Instruction to serialise.
/// @param target Target lowering information controlling operand formatting.
void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &instr, const TargetInfo &target)
{
/// @brief Implements if functionality.
/// @param MOpcode::LABEL Parameter description needed.
/// @return Return value description needed.
    if (instr.opcode == MOpcode::LABEL)
    {
/// @brief Implements if functionality.
/// @param instr.operands.empty( Parameter description needed.
/// @return Return value description needed.
        if (instr.operands.empty())
        {
            os << ".L?\n";
            return;
        }
        const auto *label = std::get_if<OpLabel>(&instr.operands.front());
/// @brief Implements if functionality.
/// @param !label Parameter description needed.
/// @return Return value description needed.
        if (!label)
        {
            os << "# <invalid label>\n";
            return;
        }
        os << label->name << ":\n";
        return;
    }

/// @brief Implements if functionality.
/// @param MOpcode::PX_COPY Parameter description needed.
/// @return Return value description needed.
    if (instr.opcode == MOpcode::PX_COPY)
    {
        std::string line;
        const auto estimate = 12U + instr.operands.size() * 24U;
        line.reserve(estimate);
        line.append("  # px_copy");
        bool first = true;
/// @brief Implements for functionality.
/// @param instr.operands Parameter description needed.
/// @return Return value description needed.
        for (const auto &operand : instr.operands)
        {
            line.append(first ? " " : ", ");
            line.append(formatOperand(operand, target));
            first = false;
        }
        line.push_back('\n');
        os << line;
        return;
    }

    const auto operands = std::span<const Operand>{instr.operands};
    const auto *row = find_encoding(instr.opcode, operands);
/// @brief Implements if functionality.
/// @param !row Parameter description needed.
/// @return Return value description needed.
    if (!row)
    {
        // Emit diagnostic comment with opcode number and operand count
        os << "  # <unknown opcode: " << static_cast<int>(instr.opcode)
           << ", operands: " << operands.size() << ">\n";
        return;
    }

/// @brief Emits _from_row.
/// @param row Parameter description needed.
/// @param operands Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
    emit_from_row(*row, operands, os, target);
}

void AsmEmitter::emit_from_row(const EncodingRow &row,
                               std::span<const Operand> operands,
                               std::ostream &os,
                               const TargetInfo &target)
{
    const auto *fmt = getFmt(row.opcode);
    const auto mnemonic = fmt ? std::string_view{fmt->mnemonic} : row.mnemonic;
/// @brief Implements if functionality.
/// @param fmt Parameter description needed.
/// @return Return value description needed.
    if (fmt)
    {
/// @brief Implements assert functionality.
/// @param row.mnemonic Parameter description needed.
/// @return Return value description needed.
        assert(mnemonic == row.mnemonic);
    }
    os << "  " << mnemonic;

/// @brief Implements if functionality.
/// @param !fmt Parameter description needed.
/// @return Return value description needed.
    if (!fmt)
    {
/// @brief Implements if functionality.
/// @param !operands.empty( Parameter description needed.
/// @return Return value description needed.
        if (!operands.empty())
        {
            os << ' ';
/// @brief Emits operands.
/// @param operands Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperands(operands, os, target);
        }
        os << '\n';
        return;
    }

/// @brief Implements if functionality.
/// @param 0U Parameter description needed.
/// @return Return value description needed.
    if (fmt->operandCount == 0U)
    {
        os << '\n';
        return;
    }

    const auto flags = fmt->flags;

/// @brief Implements if functionality.
/// @param kFmtLea Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtLea) != 0U)
    {
/// @brief Implements if functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
        if (operands.size() < 2)
        {
            os << " #<missing>\n";
            return;
        }
        os << ' ' << formatLeaSource(operands[1], target) << ", "
           << formatOperand(operands[0], target) << '\n';
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtMovzx8 Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtMovzx8) != 0U)
    {
/// @brief Implements if functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
        if (operands.size() < 2)
        {
            os << " #<missing>\n";
            return;
        }
        const auto *dest = std::get_if<OpReg>(&operands[0]);
        const auto *src = std::get_if<OpReg>(&operands[1]);
/// @brief Implements if functionality.
/// @param !src Parameter description needed.
/// @return Return value description needed.
        if (!dest || !src)
        {
            os << " #<invalid>\n";
            return;
        }
        os << ' ' << formatReg8(*src, target) << ", " << formatReg(*dest, target) << '\n';
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtCall Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtCall) != 0U)
    {
/// @brief Implements if functionality.
/// @param operands.empty( Parameter description needed.
/// @return Return value description needed.
        if (operands.empty())
        {
            os << " #<missing>\n";
            return;
        }
        os << ' ' << formatCallTarget(operands.front(), target) << '\n';
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtJump Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtJump) != 0U)
    {
/// @brief Implements if functionality.
/// @param kFmtCond Parameter description needed.
/// @return Return value description needed.
        if ((flags & kFmtCond) != 0U)
        {
            const Operand *branchTarget = nullptr;
            const OpImm *cond = nullptr;
/// @brief Implements for functionality.
/// @param operands Parameter description needed.
/// @return Return value description needed.
            for (const auto &operand : operands)
            {
/// @brief Implements if functionality.
/// @param !cond Parameter description needed.
/// @return Return value description needed.
                if (!cond)
                {
                    cond = std::get_if<OpImm>(&operand);
                }
/// @brief Implements if functionality.
/// @param std::holds_alternative<OpLabel>(operand Parameter description needed.
/// @return Return value description needed.
                if (!branchTarget && std::holds_alternative<OpLabel>(operand))
                {
                    branchTarget = &operand;
                }
            }
/// @brief Implements if functionality.
/// @param !operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (!branchTarget && !operands.empty())
            {
                branchTarget = &operands.back();
            }
            const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
            os << suffix << ' ';
/// @brief Implements if functionality.
/// @param branchTarget Parameter description needed.
/// @return Return value description needed.
            if (branchTarget)
            {
/// @brief Implements if functionality.
/// @param std::holds_alternative<OpLabel>(*branchTarget Parameter description needed.
/// @return Return value description needed.
                if (std::holds_alternative<OpLabel>(*branchTarget))
                {
                    os << formatOperand(*branchTarget, target) << '\n';
                }
                else
                {
                    os << '*' << formatOperand(*branchTarget, target) << '\n';
                }
            }
            else
            {
                os << "#<missing>\n";
            }
        }
        else
        {
/// @brief Implements if functionality.
/// @param operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (operands.empty())
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ';
            const auto &targetOp = operands.front();
/// @brief Implements if functionality.
/// @param std::holds_alternative<OpLabel>(targetOp Parameter description needed.
/// @return Return value description needed.
            if (std::holds_alternative<OpLabel>(targetOp))
            {
                os << formatOperand(targetOp, target) << '\n';
            }
            else
            {
                os << '*';
/// @brief Emits operand.
/// @param targetOp Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
                emitOperand(targetOp, os, target);
                os << '\n';
            }
        }
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtSetcc Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtSetcc) != 0U)
    {
        const Operand *dest = nullptr;
        const OpImm *cond = nullptr;
/// @brief Implements for functionality.
/// @param operands Parameter description needed.
/// @return Return value description needed.
        for (const auto &operand : operands)
        {
/// @brief Implements if functionality.
/// @param !cond Parameter description needed.
/// @return Return value description needed.
            if (!cond)
            {
                cond = std::get_if<OpImm>(&operand);
            }
/// @brief Implements if functionality.
/// @return Return value description needed.
            if (!dest &&
                (std::holds_alternative<OpReg>(operand) || std::holds_alternative<OpMem>(operand)))
            {
                dest = &operand;
            }
        }
        const auto suffix = cond ? conditionSuffix(cond->val) : std::string_view{"e"};
        os << suffix << ' ';
/// @brief Implements if functionality.
/// @param dest Parameter description needed.
/// @return Return value description needed.
        if (dest)
        {
            os << formatOperand(*dest, target) << '\n';
        }
        else
        {
            os << "#<missing>\n";
        }
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtShift Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtShift) != 0U)
    {
/// @brief Implements if functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
        if (operands.size() < 2)
        {
            os << " #<missing>\n";
            return;
        }
        os << ' ' << formatShiftCount(operands[1], target) << ", "
           << formatOperand(operands[0], target) << '\n';
        return;
    }

/// @brief Implements if functionality.
/// @param kFmtDirect Parameter description needed.
/// @return Return value description needed.
    if ((flags & kFmtDirect) != 0U)
    {
/// @brief Implements if functionality.
/// @param operands.empty( Parameter description needed.
/// @return Return value description needed.
        if (operands.empty())
        {
            os << '\n';
            return;
        }
        os << ' ';
/// @brief Emits operands.
/// @param operands Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
        emitOperands(operands, os, target);
        os << '\n';
        return;
    }

/// @brief Implements switch functionality.
/// @param fmt->operandCount Parameter description needed.
/// @return Return value description needed.
    switch (fmt->operandCount)
    {
        case 1:
        {
/// @brief Implements if functionality.
/// @param operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (operands.empty())
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ';
/// @brief Emits operand.
/// @param operands.front( Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands.front(), os, target);
            os << '\n';
            return;
        }
        case 2:
        {
/// @brief Implements if functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
            if (operands.size() < 2)
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ';
/// @brief Emits operand.
/// @param operands[1] Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands[1], os, target);
            os << ", ";
/// @brief Emits operand.
/// @param operands[0] Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands[0], os, target);
            os << '\n';
            return;
        }
        case 3:
        {
/// @brief Implements if functionality.
/// @param operands.size( Parameter description needed.
/// @return Return value description needed.
            if (operands.size() < 3)
            {
                os << " #<missing>\n";
                return;
            }
            os << ' ';
/// @brief Emits operand.
/// @param operands[2] Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands[2], os, target);
            os << ", ";
/// @brief Emits operand.
/// @param operands[1] Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands[1], os, target);
            os << ", ";
/// @brief Emits operand.
/// @param operands[0] Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperand(operands[0], os, target);
            os << '\n';
            return;
        }
        default:
        {
/// @brief Implements if functionality.
/// @param operands.empty( Parameter description needed.
/// @return Return value description needed.
            if (operands.empty())
            {
                os << '\n';
                return;
            }
            os << ' ';
/// @brief Emits operands.
/// @param operands Parameter description needed.
/// @param os Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
            emitOperands(operands, os, target);
            os << '\n';
            return;
        }
    }
}

/// @brief Convert a Machine IR operand into its assembly representation.
/// @details Dispatches on the operand variant and delegates to specialised
///          formatting helpers for registers, immediates, memory operands, and
///          labels.
/// @param operand Operand to print.
/// @param target Target lowering information controlling register names.
/// @return Textual representation of the operand.
std::string AsmEmitter::formatOperand(const Operand &operand, const TargetInfo &target)
{
    std::ostringstream buffer;
/// @brief Emits operand.
/// @param operand Parameter description needed.
/// @param buffer Parameter description needed.
/// @param target Parameter description needed.
/// @return Return value description needed.
    emitOperand(operand, buffer, target);
    return std::move(buffer).str();
}

/// @brief Format a register operand.
/// @details Returns the physical register name for hardware registers or a
///          synthetic `%vN` name for virtual registers to aid debugging.
/// @param reg Register operand to print.
/// @param target Target lowering context (unused for now but preserved for
///               future extensions).
/// @return Assembly string naming the register.
std::string AsmEmitter::formatReg(const OpReg &reg, const TargetInfo &)
{
    return asmfmt::fmt_reg(encodeRegister(reg));
}

std::string AsmEmitter::formatReg8(const OpReg &reg, const TargetInfo &target)
{
/// @brief Implements if functionality.
/// @param !reg.isPhys Parameter description needed.
/// @return Return value description needed.
    if (!reg.isPhys)
    {
        std::ostringstream os;
        os << "%v" << static_cast<unsigned>(reg.idOrPhys) << ".b";
        return os.str();
    }

    const auto phys = static_cast<PhysReg>(reg.idOrPhys);
/// @brief Implements switch functionality.
/// @param phys Parameter description needed.
/// @return Return value description needed.
    switch (phys)
    {
        case PhysReg::RAX:
            return "%al";
        case PhysReg::RBX:
            return "%bl";
        case PhysReg::RCX:
            return "%cl";
        case PhysReg::RDX:
            return "%dl";
        case PhysReg::RSI:
            return "%sil";
        case PhysReg::RDI:
            return "%dil";
        case PhysReg::RBP:
            return "%bpl";
        case PhysReg::RSP:
            return "%spl";
        case PhysReg::R8:
            return "%r8b";
        case PhysReg::R9:
            return "%r9b";
        case PhysReg::R10:
            return "%r10b";
        case PhysReg::R11:
            return "%r11b";
        case PhysReg::R12:
            return "%r12b";
        case PhysReg::R13:
            return "%r13b";
        case PhysReg::R14:
            return "%r14b";
        case PhysReg::R15:
            return "%r15b";
        default:
            return formatReg(reg, target);
    }
}

/// @brief Format an immediate operand using AT&T syntax.
/// @param imm Immediate operand to print.
/// @return Assembly string beginning with '$'.
std::string AsmEmitter::formatImm(const OpImm &imm)
{
    return asmfmt::format_imm(imm.val);
}

/// @brief Format a memory operand.
/// @details Produces the canonical `disp(base)` representation, eliding the
///          displacement when zero.
/// @param mem Memory operand to print.
/// @param target Target lowering information for register formatting.
/// @return Assembly string describing the memory reference.
std::string AsmEmitter::formatMem(const OpMem &mem, const TargetInfo &target)
{
    static_cast<void>(target);
    asmfmt::MemAddr addr{};
    addr.base = encodeRegister(mem.base);
    addr.disp = mem.disp;
/// @brief Implements if functionality.
/// @param mem.hasIndex Parameter description needed.
/// @return Return value description needed.
    if (mem.hasIndex)
    {
        addr.index = encodeRegister(mem.index);
        addr.scale = mem.scale;
        addr.has_index = true;
    }
    return asmfmt::format_mem(addr);
}

/// @brief Format a label operand.
/// @param label Label operand to print.
/// @return Raw label text.
std::string AsmEmitter::formatLabel(const OpLabel &label)
{
    return asmfmt::format_label(label.name);
}

/// @brief Format a RIP-relative label operand.
/// @param label RIP-relative label to print.
/// @return Label text suffixed with the RIP-relative addressing mode.
std::string AsmEmitter::formatRipLabel(const OpRipLabel &label)
{
    return asmfmt::format_rip_label(label.name);
}

/// @brief Format a shift count operand, rewriting RCX to CL when required.
/// @param operand Operand describing the shift count.
/// @param target Target lowering context for fallback formatting.
/// @return Assembly string for the shift count operand.
std::string AsmEmitter::formatShiftCount(const Operand &operand, const TargetInfo &target)
{
/// @brief Implements if functionality.
/// @param std::get_if<OpReg>(&operand Parameter description needed.
/// @return Return value description needed.
    if (const auto *reg = std::get_if<OpReg>(&operand))
    {
/// @brief Implements if functionality.
/// @return Return value description needed.
        if (reg->isPhys && reg->cls == RegClass::GPR &&
            reg->idOrPhys == static_cast<uint16_t>(PhysReg::RCX))
        {
            return "%cl";
        }
    }
    return formatOperand(operand, target);
}

/// @brief Format the source operand for an @c LEA instruction.
/// @details Labels are converted into RIP-relative references to match how
///          immediate addresses are encoded on x86-64.
/// @param operand Operand supplying the effective address computation.
/// @param target Target lowering context for register/memory formatting.
/// @return Assembly string representing the effective address source.
std::string AsmEmitter::formatLeaSource(const Operand &operand, const TargetInfo &target)
{
    return std::visit(Overload{[&](const OpLabel &label)
                               { return asmfmt::format_rip_label(label.name); },
                               [&](const OpMem &mem) { return formatMem(mem, target); },
                               [&](const OpReg &reg) { return formatReg(reg, target); },
                               [&](const OpImm &imm) { return formatImm(imm); },
                               [&](const OpRipLabel &label) { return formatRipLabel(label); }},
                      operand);
}

/// @brief Format the target operand for @c CALL instructions.
/// @details Ensures indirect targets are prefixed with `*` per AT&T syntax
///          while direct labels are passed through verbatim.
/// @param operand Operand describing the call target.
/// @param target Target lowering context for register/memory formatting.
/// @return Assembly string representing the call target.
std::string AsmEmitter::formatCallTarget(const Operand &operand, const TargetInfo &target)
{
    return std::visit(
        Overload{[&](const OpLabel &label)
                 { return asmfmt::format_label(viper::common::MangleLink(label.name)); },
                 [&](const OpReg &reg) { return std::string{"*"} + formatReg(reg, target); },
                 [&](const OpMem &mem) { return std::string{"*"} + formatMem(mem, target); },
                 [&](const OpImm &imm) { return formatImm(imm); },
                 [&](const OpRipLabel &label) { return std::string{"*"} + formatRipLabel(label); }},
        operand);
}

/// @brief Translate a Machine IR condition code into an x86 suffix.
/// @param code Integer encoding produced by the selector.
/// @return String view containing the condition suffix, defaulting to "e".
std::string_view AsmEmitter::conditionSuffix(std::int64_t code) noexcept
{
/// @brief Implements switch functionality.
/// @param static_cast<int>(code Parameter description needed.
/// @return Return value description needed.
    switch (static_cast<int>(code))
    {
        case 0:
            return "e";
        case 1:
            return "ne";
        case 2:
            return "l";
        case 3:
            return "le";
        case 4:
            return "g";
        case 5:
            return "ge";
        case 6:
            return "a";
        case 7:
            return "ae";
        case 8:
            return "b";
        case 9:
            return "be";
        case 10:
            return "p";
        case 11:
            return "np";
        default:
            return "e";
    }
}

} // namespace viper::codegen::x64
