//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the checker that ensures instructions produce results in the
// manner described by their opcode metadata.  The helper mirrors the
// information recorded in il/core/Opcode.def so verification stays consistent
// with the interpreter and code generation pipelines.
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

/// @brief Construct a checker bound to a specific instruction context and
///        opcode descriptor.
///
/// The context supplies instruction operands and diagnostic plumbing while the
/// descriptor conveys the expected result arity and category.
ResultTypeChecker::ResultTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Perform the result validation for the instruction described by @c ctx_.
///
/// The checker enforces result presence/absence, validates type categories, and
/// applies opcode-specific exceptions (such as narrow casts that may vary the
/// concrete result kind).  Diagnostics are emitted through @ref report.
///
/// @return Empty Expected on success or a diagnostic payload on failure.
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

/// @brief Emit a diagnostic anchored to the instruction currently being checked.
///
/// @param message Human-readable explanation of the violation.
/// @return Expected containing the constructed diagnostic.
Expected<void> ResultTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

