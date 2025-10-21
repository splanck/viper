//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/il/verify/ResultTypeChecker.cpp
// Purpose: Ensure IL instruction result presence and type adhere to opcode metadata.
// Key invariants: Result arity and kinds mirror il::core::OpcodeInfo definitions
//                 and the IL spec's typing rules.
// Ownership/Lifetime: Stateless helper capturing transient verification context.
// Links: docs/il-guide.md#reference, docs/codemap.md#il-verify
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the result-type verification helper used by the IL verifier.
/// @details The checker validates whether an instruction produces a result when
///          required and whether the result's type matches expectations from the
///          opcode metadata.  By funnelling the logic through a single helper the
///          verifier avoids drifting behaviour across different instruction
///          strategies.

#include "il/verify/ResultTypeChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/InstructionCheckUtils.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;
using il::verify::detail::kindFromCategory;
using il::core::ResultArity;
using il::core::TypeCategory;
using il::core::kindToString;

namespace il::verify::detail
{

/// @brief Construct a checker bound to a verification context and opcode metadata.
///
/// @details The constructor stores references to the context and metadata so the
///          @ref run method can access them without repeated lookups.  The
///          metadata reflects the opcode table compiled into the verifier, so the
///          checker directly interprets the required arity and type category from
///          @p info.
///
/// @param ctx Verification context describing the current instruction.
/// @param info Opcode metadata containing result-type requirements.
ResultTypeChecker::ResultTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Validate the presence and type of an instruction's result value.
///
/// @details The method first compares the instruction's result arity against the
///          opcode metadata, detecting missing or unexpected values.  Once the
///          presence requirement passes, the helper consults the metadata's
///          result type category.  Categories tied to the instruction's inferred
///          type skip explicit checking, whereas concrete categories are resolved
///          to an @ref il::core::Type::Kind via @ref kindFromCategory.  Certain
///          range-checking opcodes permit temporary mismatches because they emit
///          traps rather than results; these are skipped explicitly.  Any
///          mismatch yields a formatted diagnostic describing the expected type.
///
/// @return Empty success on validity; otherwise a structured diagnostic error.
Expected<void> ResultTypeChecker::run() const
{
    const auto &instr = ctx_.instr;
    const bool hasResult = instr.result.has_value();

    switch (info_.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
                return report("unexpected result");
            return {};
        case ResultArity::One:
            if (!hasResult)
                return report("missing result");
            break;
        case ResultArity::Optional:
            if (!hasResult)
                return {};
            break;
    }

    if (info_.resultType == TypeCategory::InstrType)
    {
        if (instr.op != il::core::Opcode::IdxChk && instr.type.kind == il::core::Type::Kind::Void)
        {
            return report("instruction type must be non-void");
        }
    }
    else if (auto expectedKind = kindFromCategory(info_.resultType))
    {
        const bool skipResultTypeCheck =
            instr.op == il::core::Opcode::CastFpToSiRteChk || instr.op == il::core::Opcode::CastFpToUiRteChk ||
            instr.op == il::core::Opcode::CastSiNarrowChk || instr.op == il::core::Opcode::CastUiNarrowChk;

        if (!skipResultTypeCheck && instr.type.kind != *expectedKind)
        {
            std::ostringstream ss;
            ss << "result type must be " << kindToString(*expectedKind);
            return report(ss.str());
        }
    }

    return {};
}

/// @brief Emit a formatted diagnostic for a result-type mismatch.
///
/// @details Packages the supplied message with context-rich information about
///          the function, block, and instruction currently under inspection so
///          downstream tooling can present actionable errors.  The helper uses
///          the same @c Expected-based channel as the rest of the verifier to
///          avoid ad-hoc error signalling.
///
/// @param message Human-readable description of the mismatch.
/// @return Expected error containing the diagnostic payload.
Expected<void> ResultTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

