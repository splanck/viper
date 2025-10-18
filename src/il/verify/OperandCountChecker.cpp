//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the operand-count verifier used by the IL validation pipeline.  The
// helper encapsulates the range checking logic for each instruction so other
// verifier components can rely on consistent diagnostics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the operand-count verification helper used by the IL verifier.
/// @details The checker compares each instruction's operand count against the
///          metadata supplied by @c OpcodeInfo and reports structured diagnostics
///          when the counts fall outside the permitted range.

#include "il/verify/OperandCountChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;

namespace il::verify::detail
{

/// @brief Construct a checker bound to a verification context and opcode metadata.
/// @details Stores references to the verification context—which exposes the
///          instruction, enclosing function, and block—and the opcode metadata
///          that records permitted operand ranges.  The checker does not copy the
///          context, keeping construction lightweight for per-instruction usage.
/// @param ctx Verification context describing the instruction under inspection.
/// @param info Opcode metadata supplying operand bounds.
OperandCountChecker::OperandCountChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Validate the operand count for the bound instruction.
/// @details Computes the number of operands carried by the instruction and
///          compares it to the opcode metadata.  Variadic opcodes relax the upper
///          bound while fixed-width opcodes require an exact or ranged match.
///          Failures funnel into @ref report to produce a consistent diagnostic.
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
/// @details Renders the instruction, including its location and parent, into a
///          diagnostic string via @ref formatInstrDiag and wraps it in an
///          @c Expected error value.  Callers propagate the error to accumulate
///          verifier failures without throwing exceptions.
/// @param message Human-readable detail describing the operand mismatch.
/// @return Expected error containing the formatted diagnostic payload.
Expected<void> OperandCountChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

