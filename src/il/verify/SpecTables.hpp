//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/SpecTables.hpp
// Purpose: Declare table-driven opcode specifications consumed by the verifier.
// Key invariants: Entries mirror the schema in src/vm/ops/schema/ops.yaml.
// Ownership/Lifetime: Returned references point to static storage.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Table-driven opcode specification interface for the IL verifier.
/// @details Exposes compact metadata describing operand counts, type
///          expectations, control-flow properties, and verification strategies
///          derived from the shared opcode schema.  The tables are generated via
///          src/il/verify/gen/specgen.py to ensure the verifier and interpreter
///          stay in sync.

#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <array>
#include <cstdint>

namespace il::verify
{

/// @brief Verification strategy applied after structural checks succeed.
enum class VerifyStrategy : uint8_t
{
    Default = 0,      ///< Record the declared result type with no extra checks.
    Alloca,           ///< Delegate to checker::checkAlloca.
    GEP,              ///< Delegate to checker::checkGEP.
    Load,             ///< Delegate to checker::checkLoad.
    Store,            ///< Delegate to checker::checkStore.
    AddrOf,           ///< Delegate to checker::checkAddrOf.
    ConstStr,         ///< Delegate to checker::checkConstStr.
    ConstNull,        ///< Delegate to checker::checkConstNull.
    Call,             ///< Delegate to checker::checkCall.
    TrapKind,         ///< Delegate to checker::checkTrapKind.
    TrapFromErr,      ///< Delegate to checker::checkTrapFromErr.
    TrapErr,          ///< Delegate to checker::checkTrapErr.
    IdxChk,           ///< Delegate to checker::checkIdxChk.
    CastFpToSiRteChk, ///< Enforce integer width constraints for fp→si casts.
    CastFpToUiRteChk, ///< Enforce integer width constraints for fp→ui casts.
    CastSiNarrowChk,  ///< Enforce result width for signed narrowing casts.
    CastUiNarrowChk,  ///< Enforce result width for unsigned narrowing casts.
    Reject,           ///< Emit a diagnostic explaining why opcode is forbidden.
    Count             ///< Sentinel enumerating the number of strategies.
};

/// @brief Schema-driven specification describing operand and control-flow rules.
struct InstructionSpec
{
    il::core::ResultArity resultArity; ///< Required result cardinality.
    il::core::TypeCategory resultType; ///< Declared result type category.
    uint8_t numOperandsMin;            ///< Minimum number of operands.
    uint8_t numOperandsMax;            ///< Maximum number of operands (or variadic sentinel).
    std::array<il::core::TypeCategory, il::core::kMaxOperandCategories>
        operandTypes;          ///< Operand type category constraints.
    bool hasSideEffects;       ///< Whether the opcode mutates observable state.
    uint8_t numSuccessors;     ///< Number of successor labels (or variadic sentinel).
    bool isTerminator;         ///< Marks block-terminating instructions.
    VerifyStrategy strategy;   ///< Post-structure verification strategy.
    const char *rejectMessage; ///< Diagnostic message for Reject strategy.
};

/// @brief Retrieve the specification associated with an opcode.
/// @param opcode Opcode to look up.
/// @return Reference to the static specification describing @p opcode.
const InstructionSpec &getInstructionSpec(il::core::Opcode opcode);

} // namespace il::verify
