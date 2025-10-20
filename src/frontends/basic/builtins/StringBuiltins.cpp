// File: src/frontends/basic/builtins/StringBuiltins.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements lowering registry for BASIC string built-in functions.
// Key invariants: Registry lookup remains deterministic and handlers honour
//                 existing runtime feature tracking semantics.
// Ownership/Lifetime: Operates on Lowerer-provided context without owning IL.
// Links: docs/codemap.md

#include "frontends/basic/builtins/StringBuiltins.hpp"

#include "frontends/basic/TypeRules.hpp"
#include "il/core/Opcode.hpp"
#include <algorithm>
#include <array>
#include <cassert>

namespace il::frontends::basic::builtins
{
namespace
{
using il::runtime::RuntimeFeature;

Value lowerLen(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::I64));
    std::vector<Value> callArgs{ctx.argValue(0)};
    return ctx.emitCallRet(Type(Type::Kind::I64), "rt_len", callArgs, ctx.call().loc);
}

Value lowerMid(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    Value source = ctx.argValue(0);
    const bool hasLength = ctx.hasArg(2);
    const il::support::SourceLoc startLoc = ctx.argLoc(1);
    ctx.ensureI64(1, startLoc);

    std::vector<Value> callArgs;
    callArgs.reserve(hasLength ? 3 : 2);
    callArgs.push_back(source);
    callArgs.push_back(ctx.argValue(1));

    const char *runtime = nullptr;
    il::support::SourceLoc callLoc = ctx.call().loc;
    if (hasLength)
    {
        const il::support::SourceLoc lengthLoc = ctx.argLoc(2);
        ctx.ensureI64(2, lengthLoc);
        callArgs.push_back(ctx.argValue(2));
        runtime = "rt_mid3";
        callLoc = lengthLoc;
        ctx.requestHelper(RuntimeFeature::Mid3);
    }
    else
    {
        runtime = "rt_mid2";
        ctx.requestHelper(RuntimeFeature::Mid2);
    }
    return ctx.emitCallRet(Type(Type::Kind::Str), runtime, callArgs, callLoc);
}

Value lowerLeft(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    Value source = ctx.argValue(0);
    const il::support::SourceLoc countLoc = ctx.argLoc(1);
    ctx.ensureI64(1, countLoc);
    std::vector<Value> callArgs{source, ctx.argValue(1)};
    ctx.requestHelper(RuntimeFeature::Left);
    return ctx.emitCallRet(Type(Type::Kind::Str), "rt_left", callArgs, ctx.call().loc);
}

Value lowerRight(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    Value source = ctx.argValue(0);
    const il::support::SourceLoc countLoc = ctx.argLoc(1);
    ctx.ensureI64(1, countLoc);
    std::vector<Value> callArgs{source, ctx.argValue(1)};
    ctx.requestHelper(RuntimeFeature::Right);
    return ctx.emitCallRet(Type(Type::Kind::Str), "rt_right", callArgs, ctx.call().loc);
}

Value lowerStr(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    if (!ctx.hasArg(0))
        return Value::constInt(0);

    const il::support::SourceLoc argLoc = ctx.argLoc(0);
    TypeRules::NumericType numericType = TypeRules::NumericType::Double;
    if (ctx.call().args[0])
        numericType = ctx.classifyNumericType(*ctx.call().args[0]);

    const char *runtime = nullptr;
    RuntimeFeature feature = RuntimeFeature::StrFromDouble;

    auto narrowInteger = [&](Type::Kind target) {
        ctx.narrowInt(0, Type(target), argLoc);
    };

    switch (numericType)
    {
        case TypeRules::NumericType::Integer:
            runtime = "rt_str_i16_alloc";
            feature = RuntimeFeature::StrFromI16;
            narrowInteger(Type::Kind::I16);
            break;
        case TypeRules::NumericType::Long:
            runtime = "rt_str_i32_alloc";
            feature = RuntimeFeature::StrFromI32;
            narrowInteger(Type::Kind::I32);
            break;
        case TypeRules::NumericType::Single:
            runtime = "rt_str_f_alloc";
            feature = RuntimeFeature::StrFromSingle;
            ctx.ensureF64(0, argLoc);
            break;
        case TypeRules::NumericType::Double:
        default:
            runtime = "rt_str_d_alloc";
            feature = RuntimeFeature::StrFromDouble;
            ctx.ensureF64(0, argLoc);
            break;
    }

    ctx.requestHelper(feature);
    std::vector<Value> callArgs{ctx.argValue(0)};
    return ctx.emitCallRet(Type(Type::Kind::Str), runtime, callArgs, ctx.call().loc);
}

Value lowerInstr(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::I64));
    std::vector<Value> callArgs;
    const bool hasStart = ctx.hasArg(2);
    const char *runtime = nullptr;
    il::support::SourceLoc callLoc = ctx.call().loc;
    if (hasStart)
    {
        const il::support::SourceLoc startLoc = ctx.argLoc(0);
        ctx.ensureI64(0, startLoc);
        ctx.addConst(0, -1, startLoc);
        callArgs = {ctx.argValue(0), ctx.argValue(1), ctx.argValue(2)};
        runtime = "rt_instr3";
        callLoc = ctx.argLoc(2);
        ctx.requestHelper(RuntimeFeature::Instr3);
    }
    else
    {
        callArgs = {ctx.argValue(0), ctx.argValue(1)};
        runtime = "rt_instr2";
        callLoc = ctx.argLoc(1);
        ctx.requestHelper(RuntimeFeature::Instr2);
    }
    return ctx.emitCallRet(Type(Type::Kind::I64), runtime, callArgs, callLoc);
}

Value lowerTrim(LowerCtx &ctx,
                ArrayRef<Value> args,
                const char *runtime,
                RuntimeFeature feature)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    std::vector<Value> callArgs{ctx.argValue(0)};
    ctx.requestHelper(feature);
    return ctx.emitCallRet(Type(Type::Kind::Str), runtime, callArgs, ctx.call().loc);
}

Value lowerLTrim(LowerCtx &ctx, ArrayRef<Value> args)
{
    return lowerTrim(ctx, args, "rt_ltrim", RuntimeFeature::Ltrim);
}

Value lowerRTrim(LowerCtx &ctx, ArrayRef<Value> args)
{
    return lowerTrim(ctx, args, "rt_rtrim", RuntimeFeature::Rtrim);
}

Value lowerTrimBoth(LowerCtx &ctx, ArrayRef<Value> args)
{
    return lowerTrim(ctx, args, "rt_trim", RuntimeFeature::Trim);
}

Value lowerUcase(LowerCtx &ctx, ArrayRef<Value> args)
{
    return lowerTrim(ctx, args, "rt_ucase", RuntimeFeature::Ucase);
}

Value lowerLcase(LowerCtx &ctx, ArrayRef<Value> args)
{
    return lowerTrim(ctx, args, "rt_lcase", RuntimeFeature::Lcase);
}

Value lowerChr(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::Str));
    const il::support::SourceLoc loc = ctx.argLoc(0);
    ctx.ensureI64(0, loc);
    std::vector<Value> callArgs{ctx.argValue(0)};
    ctx.requestHelper(RuntimeFeature::Chr);
    return ctx.emitCallRet(Type(Type::Kind::Str), "rt_chr", callArgs, ctx.call().loc);
}

Value lowerAsc(LowerCtx &ctx, ArrayRef<Value> args)
{
    (void)args;
    ctx.setResultType(Type(Type::Kind::I64));
    std::vector<Value> callArgs{ctx.argValue(0)};
    ctx.requestHelper(RuntimeFeature::Asc);
    return ctx.emitCallRet(Type(Type::Kind::I64), "rt_asc", callArgs, ctx.call().loc);
}

constexpr std::array<BuiltinSpec, 13> kStringBuiltins = {{{"LEN", 1, 1, &lowerLen},
                                                           {"MID$", 2, 3, &lowerMid},
                                                           {"LEFT$", 2, 2, &lowerLeft},
                                                           {"RIGHT$", 2, 2, &lowerRight},
                                                           {"STR$", 1, 1, &lowerStr},
                                                           {"INSTR", 2, 3, &lowerInstr},
                                                           {"LTRIM$", 1, 1, &lowerLTrim},
                                                           {"RTRIM$", 1, 1, &lowerRTrim},
                                                           {"TRIM$", 1, 1, &lowerTrimBoth},
                                                           {"UCASE$", 1, 1, &lowerUcase},
                                                           {"LCASE$", 1, 1, &lowerLcase},
                                                           {"CHR$", 1, 1, &lowerChr},
                                                           {"ASC", 1, 1, &lowerAsc}}};

} // namespace

const BuiltinSpec *findBuiltin(StringRef name)
{
    auto it = std::find_if(kStringBuiltins.begin(), kStringBuiltins.end(), [&](const BuiltinSpec &spec) {
        return spec.name == name;
    });
    if (it == kStringBuiltins.end())
        return nullptr;
    return &*it;
}

LowerCtx::LowerCtx(Lowerer &lowerer, const BuiltinCallExpr &call)
    : lowerer_(lowerer), call_(call)
{
    const std::size_t count = call.args.size();
    loweredArgs_.resize(count);
    argValues_.assign(count, Value::constInt(0));
    argLocs_.resize(count);
    hasArg_.resize(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto &expr = call.args[i];
        if (expr)
        {
            hasArg_[i] = true;
            argLocs_[i] = expr->loc;
        }
        else
        {
            hasArg_[i] = false;
            argLocs_[i] = call.loc;
        }
    }
}

/// @brief Retrieve the lowering driver powering this context.
/// @return Reference to the owning lowering driver.
Lowerer &LowerCtx::lowerer() const noexcept
{
    return lowerer_;
}

/// @brief Access the builtin call node being processed.
/// @return Immutable reference to the builtin call expression.
const BuiltinCallExpr &LowerCtx::call() const noexcept
{
    return call_;
}

/// @brief Compute the total number of argument slots available.
/// @return Number of argument positions recorded on the call.
std::size_t LowerCtx::argCount() const noexcept
{
    return call_.args.size();
}

bool LowerCtx::hasArg(std::size_t idx) const noexcept
{
    return idx < hasArg_.size() && hasArg_[idx];
}

il::support::SourceLoc LowerCtx::argLoc(std::size_t idx) const noexcept
{
    if (idx < argLocs_.size())
        return argLocs_[idx];
    return call_.loc;
}

Lowerer::RVal &LowerCtx::arg(std::size_t idx)
{
    return ensureLowered(idx);
}

Value &LowerCtx::argValue(std::size_t idx)
{
    ensureLowered(idx);
    assert(idx < argValues_.size());
    return argValues_[idx];
}

ArrayRef<Value> LowerCtx::values() noexcept
{
    return ArrayRef<Value>(argValues_.data(), argValues_.size());
}

/// @brief Record the result type that the handler will synthesize.
/// @param ty Result type chosen by the lowering routine.
void LowerCtx::setResultType(Type ty) noexcept
{
    resultType_ = ty;
}

/// @brief Query the result type previously recorded by the handler.
/// @return Type reported by the lowering routine.
Type LowerCtx::resultType() const noexcept
{
    return resultType_;
}

Lowerer::RVal &LowerCtx::ensureI64(std::size_t idx, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    Lowerer::RVal coerced = lowerer_.ensureI64(slot, loc);
    slot = coerced;
    syncValue(idx);
    return slot;
}

Lowerer::RVal &LowerCtx::ensureF64(std::size_t idx, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    Lowerer::RVal coerced = lowerer_.ensureF64(slot, loc);
    slot = coerced;
    syncValue(idx);
    return slot;
}

Lowerer::RVal &LowerCtx::coerceToI64(std::size_t idx, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    Lowerer::RVal coerced = lowerer_.coerceToI64(slot, loc);
    slot = coerced;
    syncValue(idx);
    return slot;
}

Lowerer::RVal &LowerCtx::coerceToF64(std::size_t idx, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    Lowerer::RVal coerced = lowerer_.coerceToF64(slot, loc);
    slot = coerced;
    syncValue(idx);
    return slot;
}

Lowerer::RVal &LowerCtx::addConst(std::size_t idx, std::int64_t immediate, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    lowerer_.curLoc = loc;
    slot.value = lowerer_.emitBinary(il::core::Opcode::IAddOvf,
                                     Type(Type::Kind::I64),
                                     slot.value,
                                     Value::constInt(immediate));
    slot.type = Type(Type::Kind::I64);
    syncValue(idx);
    return slot;
}

Lowerer::RVal &LowerCtx::narrowInt(std::size_t idx, Type target, il::support::SourceLoc loc)
{
    Lowerer::RVal &slot = ensureLowered(idx);
    if (slot.type.kind != target.kind)
    {
        Lowerer::RVal coerced = lowerer_.coerceToI64(slot, loc);
        slot = coerced;
        lowerer_.curLoc = loc;
        slot.value = lowerer_.emitUnary(il::core::Opcode::CastSiNarrowChk, target, slot.value);
        slot.type = target;
        syncValue(idx);
    }
    return slot;
}

TypeRules::NumericType LowerCtx::classifyNumericType(const Expr &expr)
{
    return lowerer_.classifyNumericType(expr);
}

void LowerCtx::requestHelper(RuntimeFeature feature)
{
    lowerer_.requestHelper(feature);
}

void LowerCtx::trackRuntime(RuntimeFeature feature)
{
    lowerer_.trackRuntime(feature);
}

Value LowerCtx::emitCallRet(Type ty,
                            const char *runtime,
                            const std::vector<Value> &args,
                            il::support::SourceLoc loc)
{
    lowerer_.curLoc = loc;
    return lowerer_.emitCallRet(ty, runtime, args);
}

Lowerer::RVal &LowerCtx::ensureLowered(std::size_t idx)
{
    assert(idx < loweredArgs_.size());
    auto &slot = loweredArgs_[idx];
    if (!slot)
    {
        if (hasArg(idx))
        {
            const auto &expr = call_.args[idx];
            assert(expr && "expected expression for present argument");
            slot = lowerer_.lowerExpr(*expr);
        }
        else
        {
            slot = Lowerer::RVal{Value::constInt(0), Type(Type::Kind::I64)};
        }
        argValues_[idx] = slot->value;
    }
    return *slot;
}

void LowerCtx::syncValue(std::size_t idx) noexcept
{
    assert(idx < argValues_.size());
    if (idx < loweredArgs_.size() && loweredArgs_[idx])
        argValues_[idx] = loweredArgs_[idx]->value;
}

} // namespace il::frontends::basic::builtins
