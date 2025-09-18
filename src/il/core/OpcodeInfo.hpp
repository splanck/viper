// File: src/il/core/OpcodeInfo.hpp
// Purpose: Declares metadata describing IL opcode signatures and behaviours.
// Key invariants: Table entries cover every Opcode enumerator exactly once.
// Ownership/Lifetime: Metadata is static storage duration and read-only.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Opcode.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace il::core
{

/// @brief Sentinel value representing variadic operand arity.
inline constexpr uint8_t kVariadicOperandCount = std::numeric_limits<uint8_t>::max();

/// @brief Result arity expectation for an opcode.
enum class ResultArity : uint8_t
{
    None = 0,   ///< Instruction never produces a result.
    One = 1,    ///< Instruction must produce exactly one result.
    Optional = 0xFF ///< Instruction may omit or provide a result.
};

/// @brief Type category requirement for operands or results.
enum class TypeCategory : uint8_t
{
    None,        ///< Unused slot or no constraint.
    Void,        ///< Void type (primarily for annotations).
    I1,          ///< Boolean integer type.
    I64,         ///< 64-bit integer type.
    F64,         ///< 64-bit floating point type.
    Ptr,         ///< Pointer type.
    Str,         ///< Runtime string type.
    Any,         ///< No specific type requirement.
    InstrType,   ///< Type derived from the instruction's @c type field.
    Dynamic      ///< Type derived from external context (e.g., call signature).
};

/// @brief Identifier describing VM dispatch strategy for an opcode.
enum class VMDispatch : uint8_t
{
    None,          ///< No interpreter handler implemented yet.
    Alloca,
    Load,
    Store,
    GEP,
    Add,
    Sub,
    Mul,
    Xor,
    Shl,
    FAdd,
    FSub,
    FMul,
    FDiv,
    ICmpEq,
    ICmpNe,
    SCmpGT,
    SCmpLT,
    SCmpLE,
    SCmpGE,
    FCmpEQ,
    FCmpNE,
    FCmpGT,
    FCmpLT,
    FCmpLE,
    FCmpGE,
    Br,
    CBr,
    Ret,
    AddrOf,
    ConstStr,
    Call,
    Sitofp,
    Fptosi,
    TruncOrZext1,
    Trap
};

/// @brief Maximum number of operand categories stored per opcode.
inline constexpr size_t kMaxOperandCategories = 3;

/// @brief Static description of an opcode signature and behaviour.
struct OpcodeInfo
{
    const char *name; ///< Canonical mnemonic.
    ResultArity resultArity; ///< Expected result arity.
    TypeCategory resultType; ///< Result type constraint, if any.
    uint8_t numOperandsMin; ///< Minimum operand count.
    uint8_t numOperandsMax; ///< Maximum operand count or kVariadicOperandCount.
    std::array<TypeCategory, kMaxOperandCategories> operandTypes; ///< Operand constraints.
    bool hasSideEffects; ///< Instruction mutates state or control flow.
    uint8_t numSuccessors; ///< Number of successor labels required.
    bool isTerminator; ///< Instruction terminates a block.
    VMDispatch vmDispatch; ///< Interpreter dispatch category.
};

/// @brief Metadata table indexed by @c Opcode enumerators.
extern const std::array<OpcodeInfo, kNumOpcodes> kOpcodeTable;

/// @brief Access metadata for a specific opcode.
/// @param op Opcode to query.
/// @return Reference to the metadata entry for @p op.
inline const OpcodeInfo &getOpcodeInfo(Opcode op)
{
    return kOpcodeTable[static_cast<size_t>(op)];
}

/// @brief Determine whether @p value denotes a variadic operand upper bound.
inline bool isVariadicOperandCount(uint8_t value)
{
    return value == kVariadicOperandCount;
}

} // namespace il::core
