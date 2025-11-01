//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the arithmetic instruction verification helpers.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Arithmetic instruction verification helpers.
/// @details Provides functions for ensuring arithmetic instructions obey type
///          rules and for recording result types when checks succeed.

#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/InstructionCheckerShared.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/TypeInference.hpp"

namespace il::verify::checker
{

using il::core::Type;
using il::core::Value;
using il::support::Expected;

/// @brief Translate a verifier type class into a concrete IL type kind.
/// @param typeClass Abstract type class derived from opcode metadata.
/// @return Matching IL type kind when available; otherwise empty optional.
std::optional<Type::Kind> kindFromClass(TypeClass typeClass)
{
    switch (typeClass)
    {
        case TypeClass::Void:
            return Type::Kind::Void;
        case TypeClass::I1:
            return Type::Kind::I1;
        case TypeClass::I16:
            return Type::Kind::I16;
        case TypeClass::I32:
            return Type::Kind::I32;
        case TypeClass::I64:
            return Type::Kind::I64;
        case TypeClass::F32:
            return Type::Kind::F32;
        case TypeClass::F64:
            return Type::Kind::F64;
        case TypeClass::Ptr:
            return Type::Kind::Ptr;
        case TypeClass::Str:
            return Type::Kind::Str;
        case TypeClass::Error:
            return Type::Kind::Error;
        case TypeClass::ResumeTok:
            return Type::Kind::ResumeTok;
        case TypeClass::None:
        case TypeClass::InstrType:
            return std::nullopt;
    }
    return std::nullopt;
}

/// @brief Translate a type class into a full @ref Type when possible.
/// @param typeClass Class to translate.
/// @return Concrete type or empty optional for dynamic cases.
std::optional<Type> typeFromClass(TypeClass typeClass)
{
    if (typeClass == TypeClass::InstrType)
        return std::nullopt;
    if (auto kind = kindFromClass(typeClass))
        return Type(*kind);
    return std::nullopt;
}

/// @brief Ensure every operand matches the expected type kind.
/// @details Iterates through operands and reports an error if any operand has a
///          mismatched type.
/// @param ctx Verification context containing operands.
/// @param kind Expected operand type kind.
Expected<void> expectAllOperandType(const VerifyCtx &ctx, Type::Kind kind)
{
    for (const auto &op : ctx.instr.operands)
    {
        if (ctx.types.valueType(op).kind != kind)
            return fail(ctx, "operand type mismatch");
    }
    return {};
}

/// @brief Verify a binary arithmetic instruction.
/// @details Checks operand count, ensures both operands match @p operandKind,
///          and records the provided @p resultType on success.
Expected<void> checkBinary(const VerifyCtx &ctx, Type::Kind operandKind, Type resultType)
{
    if (ctx.instr.operands.size() != 2)
        return fail(ctx, "invalid operand count");

    if (!ctx.instr.result)
        return fail(ctx, "missing result");

    if (auto result = expectAllOperandType(ctx, operandKind); !result)
        return result;

    ctx.types.recordResult(ctx.instr, resultType);
    return {};
}

/// @brief Verify a unary arithmetic instruction.
/// @details Requires exactly one operand of @p operandKind and records the
///          result type when validation passes.
Expected<void> checkUnary(const VerifyCtx &ctx, Type::Kind operandKind, Type resultType)
{
    if (ctx.instr.operands.empty())
        return fail(ctx, "invalid operand count");

    if (ctx.types.valueType(ctx.instr.operands[0]).kind != operandKind)
        return fail(ctx, "operand type mismatch");

    ctx.types.recordResult(ctx.instr, resultType);
    return {};
}

/// @brief Verify the specialised @c idx.chk instruction used for bounds checks.
/// @details Ensures operand counts and types are consistent (either all i16,
///          all i32, or all i64), validates constants for range, and records the
///          resulting integer type when the optional result annotation is present.
Expected<void> checkIdxChk(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() != 3)
        return fail(ctx, "invalid operand count");

    const auto isSupportedWidth = [](Type::Kind kind)
    { return kind == Type::Kind::I16 || kind == Type::Kind::I32 || kind == Type::Kind::I64; };

    Type::Kind expectedKind = Type::Kind::Void;
    if (isSupportedWidth(ctx.instr.type.kind))
        expectedKind = ctx.instr.type.kind;

    const auto classifyOperand = [&](const Value &value) -> Expected<Type::Kind>
    {
        if (value.kind == Value::Kind::Temp)
        {
            const Type::Kind kind = ctx.types.valueType(value).kind;
            if (kind == Type::Kind::Void)
                return failWith<Type::Kind>(ctx, "unknown temp in idx.chk");
            return kind;
        }
        if (value.kind == Value::Kind::ConstInt)
        {
            if (expectedKind == Type::Kind::Void)
            {
                if (detail::fitsInIntegerKind(value.i64, Type::Kind::I16))
                    return Type::Kind::I16;
                if (detail::fitsInIntegerKind(value.i64, Type::Kind::I32))
                    return Type::Kind::I32;
                if (detail::fitsInIntegerKind(value.i64, Type::Kind::I64))
                    return Type::Kind::I64;
                return failWith<Type::Kind>(ctx, "constant out of range for idx.chk");
            }
            if (!detail::fitsInIntegerKind(value.i64, expectedKind))
                return failWith<Type::Kind>(ctx, "constant out of range for idx.chk");
            return expectedKind;
        }
        return failWith<Type::Kind>(ctx, "operands must be i16, i32, or i64");
    };

    for (const auto &operand : ctx.instr.operands)
    {
        auto kindResult = classifyOperand(operand);
        if (!kindResult)
            return Expected<void>(kindResult.error());

        const Type::Kind operandKind = kindResult.value();
        if (!isSupportedWidth(operandKind))
            return fail(ctx, "operands must be i16, i32, or i64");

        if (expectedKind == Type::Kind::Void)
            expectedKind = operandKind;
        else if (operandKind != expectedKind)
            return fail(ctx, "operands must share integer width");
    }

    if (!isSupportedWidth(expectedKind))
        return fail(ctx, "operands must be i16, i32, or i64");

    if (ctx.instr.type.kind != Type::Kind::Void && ctx.instr.type.kind != expectedKind)
        return fail(ctx, "result type annotation must match operand width");

    ctx.types.recordResult(ctx.instr, Type(expectedKind));
    return {};
}

/// @brief Fallback verification path that simply records the instruction type.
/// @details Used when no specialised checks are required.
Expected<void> checkDefault(const VerifyCtx &)
{
    return {};
}

} // namespace il::verify::checker
