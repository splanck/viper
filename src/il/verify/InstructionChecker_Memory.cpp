// File: src/il/verify/InstructionChecker_Memory.cpp
// Purpose: Implements memory-related instruction verification helpers.
// Key invariants: Pointer and aggregate instructions validate operand/result typing rigorously.
// Ownership/Lifetime: Functions operate on VerifyCtx without taking ownership of referenced data.
// Links: docs/il-guide.md#reference

#include "il/verify/InstructionCheckerShared.hpp"

#include "il/core/Instr.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/TypeInference.hpp"

#include <cstdint>
#include <limits>

namespace il::verify::checker
{

using il::core::Type;
using il::core::Value;
using il::support::Diag;
using il::support::Expected;
using il::support::Severity;

namespace
{

void emitWarning(const VerifyCtx &ctx, std::string_view message)
{
    ctx.diags.report(Diag{Severity::Warning, formatDiag(ctx, message), ctx.instr.loc});
}

} // namespace

Expected<void> checkAlloca(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.empty())
        return fail(ctx, "missing size operand");

    if (ctx.types.valueType(ctx.instr.operands[0]).kind != Type::Kind::I64)
        return fail(ctx, "size must be i64");

    if (ctx.instr.operands[0].kind == Value::Kind::ConstInt)
    {
        const long long size = ctx.instr.operands[0].i64;
        if (size < 0)
            return fail(ctx, "negative alloca size");
        if (size > (1LL << 20))
            emitWarning(ctx, "huge alloca");
    }

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

Expected<void> checkGEP(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() < 2)
        return fail(ctx, "invalid operand count");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

Expected<void> checkLoad(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.empty())
        return fail(ctx, "missing operand");

    if (ctx.types.valueType(ctx.instr.operands[0]).kind != Type::Kind::Ptr)
        return fail(ctx, "pointer type mismatch");

    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

Expected<void> checkStore(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() < 2)
        return fail(ctx, "invalid operand count");

    bool pointerMissing = false;
    const Type pointerType = ctx.types.valueType(ctx.instr.operands[0], &pointerMissing);
    if (pointerMissing)
        return fail(ctx, "pointer operand type is unknown");

    if (pointerType.kind != Type::Kind::Ptr)
        return fail(ctx, "pointer type mismatch");

    const bool isBoolConst = ctx.instr.type.kind == Type::Kind::I1 &&
                             ctx.instr.operands[1].kind == Value::Kind::ConstInt;
    if (isBoolConst)
    {
        const long long value = ctx.instr.operands[1].i64;
        if (value != 0 && value != 1)
            return fail(ctx, "boolean store expects 0 or 1");
    }
    else if (ctx.instr.operands[1].kind == Value::Kind::ConstInt &&
             (ctx.instr.type.kind == Type::Kind::I16 || ctx.instr.type.kind == Type::Kind::I32))
    {
        const long long value = ctx.instr.operands[1].i64;
        const long long min = ctx.instr.type.kind == Type::Kind::I16 ? std::numeric_limits<int16_t>::min()
                                                                     : std::numeric_limits<int32_t>::min();
        const long long max = ctx.instr.type.kind == Type::Kind::I16 ? std::numeric_limits<int16_t>::max()
                                                                     : std::numeric_limits<int32_t>::max();
        if (value < min || value > max)
            return fail(ctx, "value out of range for store type");
    }

    return {};
}

Expected<void> checkAddrOf(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() != 1 || ctx.instr.operands[0].kind != Value::Kind::GlobalAddr)
        return fail(ctx, "operand must be global");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

Expected<void> checkConstStr(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() != 1 || ctx.instr.operands[0].kind != Value::Kind::GlobalAddr)
        return fail(ctx, "unknown string global");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Str));
    return {};
}

Expected<void> checkConstNull(const VerifyCtx &ctx)
{
    Type resultType = ctx.instr.type;
    switch (resultType.kind)
    {
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            break;
        default:
            resultType = Type(Type::Kind::Ptr);
            break;
    }

    ctx.types.recordResult(ctx.instr, resultType);
    return {};
}

} // namespace il::verify::checker
