//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/SpecTables.hpp
// Purpose: Declare opcode verification specification tables derived from schema data.
// Key invariants: Tables cover every opcode and provide stable constraints for checking.
// Ownership/Lifetime: Returned references point to constexpr storage with static lifetime.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace il::verify
{

/// @brief Reduced type classification derived from schema metadata.
enum class TypeClass : uint8_t
{
    None,
    Void,
    I1,
    I16,
    I32,
    I64,
    F64,
    Ptr,
    Str,
    Error,
    ResumeTok,
    InstrType,
};

/// @brief Signature constraints for an opcode.
struct SignatureSpec
{
    il::core::ResultArity resultArity;
    TypeClass resultType;
    uint8_t operandMin;
    uint8_t operandMax;
    std::array<TypeClass, il::core::kMaxOperandCategories> operandTypes;
};

/// @brief Control-flow and effect flags captured from the schema.
struct FlagSpec
{
    bool hasSideEffects;
    uint8_t successors;
    bool terminator;
};

/// @brief Aggregated specification for an opcode.
struct OpcodeSpec
{
    const char *mnemonic;
    SignatureSpec signature;
    FlagSpec flags;
    bool hasHandler;
};

/// @brief Enumerates post-signature verification routines.
enum class VerifyAction : uint8_t
{
    Default,
    Reject,
    IdxChk,
    Alloca,
    GEP,
    Load,
    Store,
    AddrOf,
    ConstStr,
    ConstNull,
    Call,
    TrapKind,
    TrapFromErr,
    TrapErr,
    CastFpToSiRteChk,
    CastFpToUiRteChk,
    CastSiNarrowChk,
    CastUiNarrowChk,
};

/// @brief Per-opcode verification directive.
struct VerifyRule
{
    VerifyAction action;
    const char *message;
};

/// @brief Translate a schema type class into a concrete IL type kind when available.
std::optional<il::core::Type::Kind> kindFromTypeClass(TypeClass typeClass);

/// @brief Translate a schema type class into a concrete IL type when available.
std::optional<il::core::Type> typeFromTypeClass(TypeClass typeClass);

/// @brief Retrieve the specification entry for an opcode.
const OpcodeSpec &getOpcodeSpec(il::core::Opcode opcode);

/// @brief Retrieve the verification rule for an opcode.
const VerifyRule &getVerifyRule(il::core::Opcode opcode);

} // namespace il::verify

