//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the operand-count verification helper used by the IL verifier.
/// @details The checker compares each instruction's operand count against the
/// metadata supplied by @c OpcodeInfo and reports structured diagnostics when the
/// counts fall outside the permitted range.

#include "il/verify/OperandCountChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;

namespace il::verify::detail
{

/// @brief Construct a checker bound to a verification context and opcode metadata.
/// @param ctx Verification context describing the instruction under inspection.
/// @param info Opcode metadata supplying operand bounds.
OperandCountChecker::OperandCountChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Validate the operand count for the bound instruction.
/// @details Ensures the instruction's operand count meets the minimum required
/// operands and, when not variadic, does not exceed the declared maximum.
/// @return Empty success on validity; otherwise a structured diagnostic.
Expected<void> OperandCountChecker::run() const
{
    const auto &instr = ctx_.instr;
    const size_t operandCount = instr.operands.size();
    const bool variadicOperands = il::core::isVariadicOperandCount(info_.numOperandsMax);
    if (operandCount < info_.numOperandsMin || (!variadicOperands && operandCount > info_.numOperandsMax))
    {
        std::ostringstream ss;
        if (info_.numOperandsMin == info_.numOperandsMax && !variadicOperands)
        {
            ss << "expected " << static_cast<unsigned>(info_.numOperandsMin) << " operand";
            if (info_.numOperandsMin != 1)
                ss << 's';
        }
        else if (variadicOperands)
        {
            ss << "expected at least " << static_cast<unsigned>(info_.numOperandsMin) << " operand";
            if (info_.numOperandsMin != 1)
                ss << 's';
        }
        else
        {
            ss << "expected between " << static_cast<unsigned>(info_.numOperandsMin) << " and "
               << static_cast<unsigned>(info_.numOperandsMax) << " operands";
        }
        return report(ss.str());
    }

    return {};
}

/// @brief Emit a diagnostic constructed from the supplied message.
/// @details Formats the instruction context into a user-facing string and wraps
/// it in an @c Expected error value so callers can propagate failure uniformly.
/// @param message Human-readable detail describing the operand mismatch.
/// @return Expected error containing the formatted diagnostic payload.
Expected<void> OperandCountChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

