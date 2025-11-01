//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lowering support for BASIC string builtins.  Specialised lowering
// delegates to the shared string builtin registry when available and falls back
// to the rule-driven pipeline otherwise.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/BuiltinCommon.hpp"
#include "frontends/basic/lower/builtins/Registrars.hpp"

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/builtins/StringBuiltins.hpp"

#include <cassert>
#include <limits>
#include <vector>

namespace il::frontends::basic::lower
{
namespace
{
using IlType = il::core::Type;
using IlKind = IlType::Kind;
using Value = il::core::Value;
using Variant = BuiltinLoweringRule::Variant;
using Opcode = il::core::Opcode;
} // namespace

Lowerer::RVal lowerValBuiltin(BuiltinLowerContext &ctx, const Variant &variant)
{
    assert(!variant.arguments.empty());
    const auto &argSpec = variant.arguments.front();
    Lowerer::RVal &argVal = ctx.applyTransforms(argSpec, argSpec.transforms);
    const il::support::SourceLoc conversionLoc = ctx.callLoc(variant.callLocArg);

    ctx.setCurrentLoc(conversionLoc);
    Value cstr = ctx.emitCall(IlType(IlKind::Ptr), "rt_string_cstr", {argVal.value});

    Value okSlot = ctx.emitAlloca(1);
    std::vector<Value> callArgs{cstr, okSlot};
    IlType resultType = ctx.resolveResultType();
    ctx.setCurrentLoc(conversionLoc);
    Value callRes = ctx.emitCall(resultType, variant.runtime, callArgs);

    ctx.setCurrentLoc(conversionLoc);
    Value okVal = ctx.emitLoad(ctx.boolType(), okSlot);

    BuiltinLowerContext::ValBlocks blocks = ctx.createValBlocks();
    if (!blocks.cont || !blocks.trap || !blocks.nan || !blocks.overflow)
        return {callRes, resultType};

    ctx.emitCBr(okVal, blocks.cont, blocks.trap);

    ctx.setCurrentBlock(blocks.trap);
    ctx.setCurrentLoc(conversionLoc);
    Value isNan = ctx.emitBinary(Opcode::FCmpNE, ctx.boolType(), callRes, callRes);
    ctx.emitCBr(isNan, blocks.nan, blocks.overflow);

    ctx.setCurrentBlock(blocks.nan);
    ctx.emitConversionTrap(conversionLoc);

    ctx.setCurrentBlock(blocks.overflow);
    ctx.setCurrentLoc(conversionLoc);
    Value overflowSentinel = ctx.emitUnary(Opcode::CastFpToSiRteChk,
                                           IlType(IlKind::I64),
                                           Value::constFloat(std::numeric_limits<double>::max()));
    (void)overflowSentinel;
    ctx.emitTrap();

    ctx.setCurrentBlock(blocks.cont);
    return {callRes, resultType};
}

} // namespace il::frontends::basic::lower

namespace il::frontends::basic::lower::builtins
{
namespace
{
using Builtin = BuiltinCallExpr::Builtin;
namespace string_builtins = il::frontends::basic::builtins;

Lowerer::RVal lowerStringBuiltin(BuiltinLowerContext &ctx)
{
    const auto *stringSpec = string_builtins::findBuiltin(ctx.info().name);
    if (!stringSpec)
        return lowerGenericBuiltin(ctx);

    const std::size_t argCount = ctx.call().args.size();
    if (argCount < static_cast<std::size_t>(stringSpec->minArity) ||
        argCount > static_cast<std::size_t>(stringSpec->maxArity))
        return lowerGenericBuiltin(ctx);

    string_builtins::LowerCtx strCtx(ctx.lowerer(), ctx.call());
    il::core::Value resultValue = stringSpec->fn(strCtx, strCtx.values());
    return {resultValue, strCtx.resultType()};
}

} // namespace

void registerStringBuiltins()
{
    register_builtin(getBuiltinInfo(Builtin::Len).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Mid).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Left).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Right).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Str).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Instr).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Ltrim).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Rtrim).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Trim).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Ucase).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Lcase).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Chr).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::Asc).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::InKey).name, &lowerStringBuiltin);
    register_builtin(getBuiltinInfo(Builtin::GetKey).name, &lowerStringBuiltin);
}

} // namespace il::frontends::basic::lower::builtins
