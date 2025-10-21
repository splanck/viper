//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/il/verify/OperandCountChecker.cpp
// Purpose: Validate IL instruction operand cardinality against opcode metadata.
// Key invariants: Operand bounds align exactly with il::core::OpcodeInfo
//                 definitions; diagnostics use structured verifier formatting.
// Ownership/Lifetime: Stateless helper bound to transient verification contexts.
// Links: docs/il-guide.md#reference, docs/codemap.md#il-verify
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the operand-count verification helper used by the IL verifier.
/// @details The checker compares each instruction's operand count against the
///          metadata supplied by @c OpcodeInfo and reports structured diagnostics
///          when the counts fall outside the permitted range.  Centralising the
///          logic here keeps the operand checker consistent with any future
///          metadata updates.

#include "il/verify/OperandCountChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;

namespace il::verify::detail
{

/// @brief Construct a checker bound to a verification context and opcode metadata.
///
/// @details The constructor simply snapshots the references supplied by the
///          caller.  Binding both the verifier context and the opcode metadata
///          allows @ref run to operate without chasing optional pointers or
///          repeatedly looking up the same opcode information.
///
/// @param ctx Verification context describing the instruction under inspection.
/// @param info Opcode metadata supplying operand bounds.
OperandCountChecker::OperandCountChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

/// @brief Validate the operand count for the bound instruction.
///
/// @details Fetches the instruction from the verification context, counts the
///          provided operands, and compares that count to the metadata-specified
///          minimum and maximum.  Variadic instructions advertise a sentinel
///          maximum; in that case only the minimum bound is enforced.  When a
///          mismatch is detected the helper constructs a targeted diagnostic that
///          explains the expected arity before returning it through the
///          @c Expected error channel.
///
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
///
/// @details Uses @ref formatInstrDiag to include the function, block, and
///          instruction in the error text while preserving source locations.  By
///          funnelling all error creation through this helper the checker keeps
///          formatting consistent with the rest of the verifier.
///
/// @param message Human-readable detail describing the operand mismatch.
/// @return Expected error containing the formatted diagnostic payload.
Expected<void> OperandCountChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

