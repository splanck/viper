//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares shared utilities and specialized checkers used by table-driven
// instruction verification. It provides the common infrastructure that implements
// the verification strategies referenced by the SpecTables metadata.
//
// The table-driven verification system separates opcode metadata (operand counts,
// type categories, verification strategies) from the implementation of those
// strategies. This file contains the strategy implementations - specialized checking
// functions for memory operations, arithmetic instructions, casts, bounds checks,
// and runtime calls - referenced by the VerifyStrategy enum in SpecTables.
//
// Key Responsibilities:
// - Provide diagnostic formatting helpers for consistent error messages
// - Implement specialized checkers for memory operations (alloca, load, store, gep)
// - Validate runtime call instructions (call.extern, call.func)
// - Check runtime error handling operations (trap, trap.err, trap.from.err)
// - Verify arithmetic and cast operations with proper type constraints
// - Implement the default checker for simple instructions
//
// Design Rationale:
// The il::verify::checker namespace groups strategy implementations separately from
// the dispatch logic. Each checker accepts a VerifyCtx containing all verification
// state (function, block, instruction, type environment, diagnostics) and returns
// Expected<void> for uniform error propagation. Helper functions like formatDiag()
// and fail() reduce boilerplate in checker implementations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/VerifierTable.hpp"
#include "il/verify/VerifyCtx.hpp"
#include "support/diag_expected.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace il::verify::checker
{

using il::core::Type;
using il::support::Expected;
using il::support::makeError;

/// @brief Format a diagnostic message for the current instruction context.
/// @param ctx Verification context providing function, block, and instruction.
/// @param message Human-readable text describing the issue.
/// @return Fully formatted diagnostic string.
inline std::string formatDiag(const VerifyCtx &ctx, std::string_view message)
{
    return formatInstrDiag(ctx.fn, ctx.block, ctx.instr, message);
}

/// @brief Construct an error Expected value with a formatted diagnostic.
/// @param ctx Verification context that supplies diagnostic metadata.
/// @param message Human-readable text describing the issue.
/// @return Expected holding a diagnostic error populated with @p message.
inline Expected<void> fail(const VerifyCtx &ctx, std::string message)
{
    return Expected<void>(makeError(ctx.instr.loc, formatDiag(ctx, message)));
}

/// @brief Helper to build an error Expected<T> with a formatted diagnostic.
/// @tparam T Desired Expected value type.
/// @param ctx Verification context that supplies diagnostic metadata.
/// @param message Human-readable text describing the issue.
/// @return Expected<T> holding a diagnostic error populated with @p message.
template <typename T> inline Expected<T> failWith(const VerifyCtx &ctx, std::string message)
{
    return Expected<T>(makeError(ctx.instr.loc, formatDiag(ctx, message)));
}

/// @brief Translate a verifier table type class into a concrete kind when available.
/// @param typeClass Classification to translate.
/// @return Matching kind or std::nullopt when the class is dynamic.
std::optional<Type::Kind> kindFromClass(TypeClass typeClass);

/// @brief Translate a verifier table type class into a concrete type when available.
/// @param typeClass Classification to translate.
/// @return Matching type or std::nullopt when the class depends on instruction metadata.
std::optional<Type> typeFromClass(TypeClass typeClass);

// Arithmetic helpers.
Expected<void> expectAllOperandType(const VerifyCtx &ctx, Type::Kind kind);
Expected<void> checkBinary(const VerifyCtx &ctx, Type::Kind operandKind, Type resultType);
Expected<void> checkUnary(const VerifyCtx &ctx, Type::Kind operandKind, Type resultType);
Expected<void> checkIdxChk(const VerifyCtx &ctx);

// Memory helpers.
Expected<void> checkAlloca(const VerifyCtx &ctx);
Expected<void> checkGEP(const VerifyCtx &ctx);
Expected<void> checkLoad(const VerifyCtx &ctx);
Expected<void> checkStore(const VerifyCtx &ctx);
Expected<void> checkAddrOf(const VerifyCtx &ctx);
Expected<void> checkConstStr(const VerifyCtx &ctx);
Expected<void> checkConstNull(const VerifyCtx &ctx);

// Runtime helpers.
Expected<void> checkCall(const VerifyCtx &ctx);
Expected<void> checkTrapKind(const VerifyCtx &ctx);
Expected<void> checkTrapErr(const VerifyCtx &ctx);
Expected<void> checkTrapFromErr(const VerifyCtx &ctx);

/// @brief Default validator that records the declared instruction result type.
/// @param ctx Verification context that owns the instruction.
/// @return Always empty because structural checks handle failures.
Expected<void> checkDefault(const VerifyCtx &ctx);

} // namespace il::verify::checker
