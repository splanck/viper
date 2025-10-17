//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the result type checker used during IL verification.  The helper
// enforces result arity and type expectations derived from opcode metadata.
//
//===----------------------------------------------------------------------===//

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

/// @brief Construct a result type checker bound to a verification context.
///
/// Stores references to the caller's verification context and opcode metadata
/// so the checker can inspect instruction state without copying data.
///
/// @param ctx  Verification context describing the instruction under test.
/// @param info Opcode metadata supplying result arity and type requirements.
ResultTypeChecker::ResultTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{ 
}

/// @brief Execute result arity and type validation for the current instruction.
///
/// Checks that the instruction's optional result matches the opcode's arity
/// requirements and, when applicable, that the declared type matches the
/// expected category.  Certain opcodes allow dynamic result typing and are
/// handled explicitly.
///
/// @return Success when the result satisfies metadata requirements; otherwise a diagnostic failure.
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

/// @brief Emit a formatted diagnostic describing a result type mismatch.
///
/// Wraps @ref makeError to attach function, block, and instruction context to
/// the provided message.
///
/// @param message Human-readable description of the failure.
/// @return Diagnostic encapsulated in an @ref Expected failure.
Expected<void> ResultTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

