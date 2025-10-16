// File: src/il/verify/OperandTypeChecker.cpp
// Purpose: Implements a helper that validates operand types against opcode metadata.
// Key invariants: Operates on the verification context for a single instruction.
// Ownership/Lifetime: Non-owning references to verification data structures.
// Links: docs/il-guide.md#reference

#include "il/verify/OperandTypeChecker.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/TypeInference.hpp"

#include <sstream>

using il::support::Expected;
using il::support::makeError;
using il::verify::detail::fitsInIntegerKind;
using il::verify::detail::kindFromCategory;
using il::core::TypeCategory;
using il::core::kindToString;

namespace il::verify::detail
{

OperandTypeChecker::OperandTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{
}

Expected<void> OperandTypeChecker::run() const
{
    const auto &instr = ctx_.instr;

    for (size_t index = 0; index < instr.operands.size() && index < info_.operandTypes.size(); ++index)
    {
        const TypeCategory category = info_.operandTypes[index];
        if (category == TypeCategory::None || category == TypeCategory::Any || category == TypeCategory::Dynamic)
            continue;

        il::core::Type::Kind expectedKind;
        if (category == TypeCategory::InstrType)
        {
            if (instr.type.kind == il::core::Type::Kind::Void)
            {
                return report("instruction type must be non-void");
            }
            expectedKind = instr.type.kind;
        }
        else if (auto mapped = kindFromCategory(category))
        {
            expectedKind = *mapped;
        }
        else
        {
            continue;
        }

        const auto &operand = instr.operands[index];
        if (operand.kind == il::core::Value::Kind::ConstInt)
        {
            switch (expectedKind)
            {
                case il::core::Type::Kind::I1:
                    if (!fitsInIntegerKind(operand.i64, expectedKind))
                    {
                        std::ostringstream ss;
                        ss << "operand " << index << " constant out of range for i1";
                        return report(ss.str());
                    }
                    continue;
                case il::core::Type::Kind::I16:
                case il::core::Type::Kind::I32:
                case il::core::Type::Kind::I64:
                    if (!fitsInIntegerKind(operand.i64, expectedKind))
                    {
                        std::ostringstream ss;
                        ss << "operand " << index << " constant out of range for "
                           << kindToString(expectedKind);
                        return report(ss.str());
                    }
                    continue;
                default:
                    break;
            }
        }

        bool missing = false;
        const il::core::Type actual = ctx_.types.valueType(operand, &missing);
        if (missing)
        {
            std::ostringstream ss;
            ss << "operand " << index << " type is unknown";
            return report(ss.str());
        }

        if (actual.kind != expectedKind)
        {
            std::ostringstream ss;
            if (expectedKind == il::core::Type::Kind::Ptr)
            {
                ss << "pointer type mismatch";
            }
            else
            {
                ss << "operand " << index << " must be " << kindToString(expectedKind);
            }
            return report(ss.str());
        }
    }

    return {};
}

Expected<void> OperandTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

