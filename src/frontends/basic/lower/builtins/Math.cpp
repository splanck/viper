//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lowering and registration for BASIC math-oriented builtins.  The
// lowering logic routes through the generic variant dispatcher while providing
// specialised handling for runtime conversions that require guard blocks.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

#include "frontends/basic/BuiltinRegistry.hpp"

#include <cassert>
#include <vector>

namespace il::frontends::basic::lower
{
namespace
{
using IlType = il::core::Type;
using IlKind = IlType::Kind;
using Value = il::core::Value;
using Variant = BuiltinLoweringRule::Variant;
} // namespace

Lowerer::RVal lowerConversionBuiltinImpl(BuiltinLowerContext &ctx)
{
    const Variant *variant = ctx.selectVariant();
    if (!variant)
        return ctx.makeZeroResult();

    Lowerer::RVal result = ctx.makeZeroResult();
    switch (ctx.call().builtin)
    {
        case BuiltinCallExpr::Builtin::Cint:
            result =
                lowerNumericConversion(ctx, *variant, IlType(IlKind::I64), "cint_ok", "cint_trap");
            break;
        case BuiltinCallExpr::Builtin::Clng:
            result =
                lowerNumericConversion(ctx, *variant, IlType(IlKind::I64), "clng_ok", "clng_trap");
            break;
        case BuiltinCallExpr::Builtin::Csng:
            result =
                lowerNumericConversion(ctx, *variant, IlType(IlKind::F64), "csng_ok", "csng_trap");
            break;
        case BuiltinCallExpr::Builtin::Val:
            result = lowerValBuiltin(ctx, *variant);
            break;
        default:
            result = emitBuiltinVariant(ctx, *variant);
            break;
    }

    ctx.applyFeatures(*variant);
    return result;
}

Lowerer::RVal lowerNumericConversion(BuiltinLowerContext &ctx,
                                     const Variant &variant,
                                     IlType resultType,
                                     const char *contHint,
                                     const char *trapHint)
{
    assert(!variant.arguments.empty());
    const auto &argSpec = variant.arguments.front();
    Lowerer::RVal &argVal = ctx.applyTransforms(argSpec, argSpec.transforms);
    const il::support::SourceLoc callLoc = ctx.callLoc(variant.callLocArg);

    Value okSlot = ctx.emitAlloca(1);
    std::vector<Value> callArgs{argVal.value, okSlot};
    ctx.setCurrentLoc(callLoc);
    Value callRes = ctx.emitCall(resultType, variant.runtime, callArgs);

    ctx.setCurrentLoc(callLoc);
    Value okVal = ctx.emitLoad(ctx.boolType(), okSlot);

    BuiltinLowerContext::BranchPair guards = ctx.createGuardBlocks(contHint, trapHint);
    if (!guards.cont || !guards.trap)
        return {callRes, resultType};

    ctx.emitCBr(okVal, guards.cont, guards.trap);

    ctx.setCurrentBlock(guards.trap);
    ctx.emitConversionTrap(callLoc);

    ctx.setCurrentBlock(guards.cont);
    return {callRes, resultType};
}

} // namespace il::frontends::basic::lower

namespace il::frontends::basic::lower::builtins
{
namespace
{
using Builtin = BuiltinCallExpr::Builtin;

Lowerer::RVal lowerMathBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

Lowerer::RVal lowerConversionBuiltin(BuiltinLowerContext &ctx)
{
    return lowerConversionBuiltinImpl(ctx);
}

} // namespace

void registerMathBuiltins()
{
    register_builtin(getBuiltinInfo(Builtin::Cdbl).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Int).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Fix).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Round).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Sqr).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Abs).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Floor).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Ceil).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Sin).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Cos).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Pow).name, &lowerMathBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Rnd).name, &lowerMathBuiltin);
}

void registerConversionBuiltins()
{
    register_builtin(getBuiltinInfo(Builtin::Val).name, &lowerConversionBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Cint).name, &lowerConversionBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Clng).name, &lowerConversionBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Csng).name, &lowerConversionBuiltin);
}

} // namespace il::frontends::basic::lower::builtins
