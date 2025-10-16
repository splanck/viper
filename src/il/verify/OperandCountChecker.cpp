// File: src/il/verify/OperandCountChecker.cpp
// Purpose: Implements a helper that validates operand counts against opcode metadata.
// Key invariants: Operates on the verification context for a single instruction.
// Ownership/Lifetime: Non-owning references to verification data structures.
// Links: docs/il-guide.md#reference

#include "il/verify/OperandCountChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;

namespace il::verify::detail
{

OperandCountChecker::OperandCountChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

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

Expected<void> OperandCountChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

