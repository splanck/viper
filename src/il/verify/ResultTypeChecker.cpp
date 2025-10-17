//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the result-type verification helper used by the IL verifier.
/// @details The checker validates whether an instruction produces a result when
/// required and whether the result's type matches expectations from the opcode
/// metadata.

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
/// @param ctx Verification context describing the current instruction.
/// @param info Opcode metadata containing result-type requirements.
ResultTypeChecker::ResultTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Validate the presence and type of an instruction's result value.
/// @details Confirms mandatory results are emitted, optional results are allowed
/// to be absent, and that typed results use the expected IL type when enforced by
/// the opcode metadata.
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
/// @details Wraps the diagnostic in an @c Expected error so callers can propagate
/// verification failures uniformly.
/// @param message Human-readable description of the mismatch.
/// @return Expected error containing the diagnostic payload.
Expected<void> ResultTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

