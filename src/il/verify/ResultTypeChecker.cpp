// File: src/il/verify/ResultTypeChecker.cpp
// Purpose: Implements a helper that validates result presence and types against opcode metadata.
// Key invariants: Operates on the verification context for a single instruction.
// Ownership/Lifetime: Non-owning references to verification data structures.
// Links: docs/il-guide.md#reference

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

ResultTypeChecker::ResultTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

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

Expected<void> ResultTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

