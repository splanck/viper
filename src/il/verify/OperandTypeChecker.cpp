//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the operand type checker used by the IL verifier.  The helper
// compares operand categories from opcode metadata against inferred operand
// types, reporting diagnostics when mismatches occur.
//
//===----------------------------------------------------------------------===//

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

/// @brief Construct an operand type checker for a specific verification context.
///
/// Stores references to the caller's verification context and opcode metadata
/// so subsequent checks can pull type information without copying data.
///
/// @param ctx  Verification context describing the instruction under test.
/// @param info Opcode metadata providing operand category expectations.
OperandTypeChecker::OperandTypeChecker(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
    : ctx_(ctx), info_(info)
{ 
}

/// @brief Execute the operand type validation process.
///
/// Iterates over declared operand categories, resolves the actual operand
/// types via @ref TypeInference, and ensures constants remain within the
/// representable range.  A diagnostic is produced for the first mismatch.
///
/// @return Success when all operands satisfy the metadata requirements.
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

/// @brief Emit a formatted diagnostic describing an operand type mismatch.
///
/// Convenience helper that wraps @ref makeError with instruction context.
///
/// @param message Human-readable description of the mismatch.
/// @return Diagnostic encapsulated in an @ref Expected failure.
Expected<void> OperandTypeChecker::report(std::string_view message) const
{
    return Expected<void>{makeError(ctx_.instr.loc, formatInstrDiag(ctx_.fn, ctx_.block, ctx_.instr, message))};
}

} // namespace il::verify::detail

