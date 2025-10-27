// File: src/il/verify/InstructionCheckerShared.hpp
// Purpose: Provides shared helpers and declarations for instruction verification submodules.
// Key invariants: Helpers operate on the current verification context without modifying ownership.
// Ownership/Lifetime: Functions reference caller-managed context and diagnostics only for the call
// duration. Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/SpecTables.hpp"
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
