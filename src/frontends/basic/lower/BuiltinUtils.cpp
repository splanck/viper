//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides shared utilities used by BASIC builtin lowering helpers.  The
// BuiltinLowerContext wrapper exposes argument coercion helpers, result-type
// resolution, and runtime feature tracking so family-specific lowering routines
// can focus on their bespoke control flow without duplicating boilerplate.
// A registry maps builtin names to the appropriate lowering function, replacing
// the monolithic if/else cascade that previously handled dispatch.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared implementation for BASIC builtin lowering helpers.
/// @details Implements @ref BuiltinLowerContext methods alongside helper
///          routines used by builtin families (math, string, conversions, etc.).
///          The registry exported from this translation unit allows the caller
///          to dispatch builtins via a table lookup rather than a large
///          conditional tree.

#include "frontends/basic/lower/BuiltinUtils.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/builtins/StringBuiltins.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_location.hpp"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <limits>
#include <utility>

namespace il::frontends::basic::lower
{
namespace
{
using IlType = il::core::Type;
using IlKind = IlType::Kind;
using Value = il::core::Value;
using Opcode = il::core::Opcode;
using Variant = BuiltinLoweringRule::Variant;
using Transform = BuiltinLoweringRule::ArgTransform;
using TransformKind = BuiltinLoweringRule::ArgTransform::Kind;
using Feature = BuiltinLoweringRule::Feature;
using FeatureAction = BuiltinLoweringRule::Feature::Action;

Lowerer::RVal lowerGenericBuiltin(BuiltinLowerContext &ctx);
Lowerer::RVal lowerConversionBuiltinImpl(BuiltinLowerContext &ctx);
Lowerer::RVal emitVariant(BuiltinLowerContext &ctx, const Variant &variant);
Lowerer::RVal emitCallRuntime(BuiltinLowerContext &ctx, const Variant &variant);
Lowerer::RVal emitUnary(BuiltinLowerContext &ctx, const Variant &variant);
Lowerer::RVal emitCustom(BuiltinLowerContext &ctx, const Variant &variant);
Lowerer::RVal lowerNumericConversion(BuiltinLowerContext &ctx,
                                     const Variant &variant,
                                     IlType resultType,
                                     const char *okHint,
                                     const char *trapHint);
Lowerer::RVal lowerValBuiltin(BuiltinLowerContext &ctx, const Variant &variant);

} // namespace

BuiltinLowerContext::BuiltinLowerContext(Lowerer &lowerer, const BuiltinCallExpr &call)
    : lowerer_(&lowerer),
      call_(&call),
      rule_(&getBuiltinLoweringRule(call.builtin)),
      info_(&getBuiltinInfo(call.builtin)),
      originalTypes_(call.args.size()),
      argLocs_(call.args.size()),
      loweredArgs_(call.args.size())
{
    for (std::size_t i = 0; i < call.args.size(); ++i)
    {
        const auto &arg = call.args[i];
        if (!arg)
            continue;
        argLocs_[i] = arg->loc;
        originalTypes_[i] = lowerer.scanExpr(*arg);
    }
}

bool BuiltinLowerContext::hasArg(std::size_t idx) const noexcept
{
    return idx < call_->args.size() && call_->args[idx] != nullptr;
}

std::optional<Lowerer::ExprType> BuiltinLowerContext::originalType(std::size_t idx) const noexcept
{
    if (idx >= originalTypes_.size())
        return std::nullopt;
    return originalTypes_[idx];
}

il::support::SourceLoc BuiltinLowerContext::argLoc(std::size_t idx) const noexcept
{
    if (idx < argLocs_.size() && argLocs_[idx])
        return *argLocs_[idx];
    return call_->loc;
}

il::support::SourceLoc BuiltinLowerContext::callLoc(const std::optional<std::size_t> &idx) const noexcept
{
    if (idx && *idx < argLocs_.size() && argLocs_[*idx])
        return *argLocs_[*idx];
    return call_->loc;
}

Lowerer::RVal &BuiltinLowerContext::ensureLowered(std::size_t idx)
{
    assert(hasArg(idx) && "builtin lowering referenced missing argument");
    auto &slot = loweredArgs_[idx];
    if (!slot)
        slot = lowerer_->lowerExpr(*call_->args[idx]);
    return *slot;
}

Lowerer::RVal &BuiltinLowerContext::appendSynthetic(Lowerer::RVal value)
{
    syntheticArgs_.push_back(std::move(value));
    return syntheticArgs_.back();
}

Lowerer::RVal &BuiltinLowerContext::ensureArgument(const BuiltinLoweringRule::Argument &spec)
{
    const std::size_t idx = spec.index;
    if (hasArg(idx))
        return ensureLowered(idx);
    if (spec.defaultValue)
    {
        const auto &def = *spec.defaultValue;
        Lowerer::RVal value{Value::constInt(def.i64), IlType(IlKind::I64)};
        switch (def.type)
        {
            case Lowerer::ExprType::F64:
                value = {Value::constFloat(def.f64), IlType(IlKind::F64)};
                break;
            case Lowerer::ExprType::Str:
                assert(false && "string default values are not supported");
                break;
            case Lowerer::ExprType::Bool:
                value = {lowerer_->emitBoolConst(def.i64 != 0), lowerer_->ilBoolTy()};
                break;
            case Lowerer::ExprType::I64:
            default:
                value = {Value::constInt(def.i64), IlType(IlKind::I64)};
                break;
        }
        return appendSynthetic(std::move(value));
    }
    assert(false && "builtin lowering referenced missing argument without default");
    return appendSynthetic({Value::constInt(0), IlType(IlKind::I64)});
}

il::support::SourceLoc BuiltinLowerContext::selectArgLoc(const BuiltinLoweringRule::Argument &spec) const
{
    if (spec.index < argLocs_.size() && argLocs_[spec.index])
        return *argLocs_[spec.index];
    return call_->loc;
}

Lowerer::RVal &BuiltinLowerContext::applyTransforms(
    const BuiltinLoweringRule::Argument &spec,
    const std::vector<BuiltinLoweringRule::ArgTransform> &transforms)
{
    Lowerer::RVal &slot = ensureArgument(spec);
    il::support::SourceLoc loc = selectArgLoc(spec);
    for (const auto &transform : transforms)
    {
        switch (transform.kind)
        {
            case TransformKind::EnsureI64:
                slot = lowerer_->ensureI64(std::move(slot), loc);
                break;
            case TransformKind::EnsureF64:
                slot = lowerer_->ensureF64(std::move(slot), loc);
                break;
            case TransformKind::EnsureI32:
                slot = lowerer_->ensureI64(std::move(slot), loc);
                if (slot.type.kind != IlKind::I32)
                {
                    lowerer_->curLoc = loc;
                    slot.value = lowerer_->emitUnary(Opcode::CastSiNarrowChk, IlType(IlKind::I32), slot.value);
                    slot.type = IlType(IlKind::I32);
                }
                break;
            case TransformKind::CoerceI64:
                slot = lowerer_->coerceToI64(std::move(slot), loc);
                break;
            case TransformKind::CoerceF64:
                slot = lowerer_->coerceToF64(std::move(slot), loc);
                break;
            case TransformKind::CoerceBool:
                slot = lowerer_->coerceToBool(std::move(slot), loc);
                break;
            case TransformKind::AddConst:
                lowerer_->curLoc = loc;
                slot.value = lowerer_->emitBinary(Opcode::IAddOvf,
                                                  IlType(IlKind::I64),
                                                  slot.value,
                                                  Value::constInt(transform.immediate));
                slot.type = IlType(IlKind::I64);
                break;
        }
    }
    return slot;
}

IlType BuiltinLowerContext::typeFromExpr(Lowerer &lowerer, Lowerer::ExprType type)
{
    switch (type)
    {
        case Lowerer::ExprType::F64:
            return IlType(IlKind::F64);
        case Lowerer::ExprType::Str:
            return IlType(IlKind::Str);
        case Lowerer::ExprType::Bool:
            return lowerer.ilBoolTy();
        case Lowerer::ExprType::I64:
        default:
            return IlType(IlKind::I64);
    }
}

IlType BuiltinLowerContext::resolveResultType(const BuiltinLoweringRule::ResultSpec &spec)
{
    switch (spec.kind)
    {
        case BuiltinLoweringRule::ResultSpec::Kind::Fixed:
            return typeFromExpr(*lowerer_, spec.type);
        case BuiltinLoweringRule::ResultSpec::Kind::FromArg:
            if (hasArg(spec.argIndex))
                return ensureLowered(spec.argIndex).type;
            return typeFromExpr(*lowerer_, spec.type);
    }
    return IlType(IlKind::I64);
}

IlType BuiltinLowerContext::resolveResultType()
{
    return resolveResultType(rule_->result);
}

Lowerer::RVal BuiltinLowerContext::makeZeroResult() const
{
    return {Value::constInt(0), IlType(IlKind::I64)};
}

const BuiltinLoweringRule::Variant *BuiltinLowerContext::selectVariant() const
{
    const auto &variants = rule_->variants;
    const Variant *selected = nullptr;
    for (const auto &candidate : variants)
    {
        bool matches = false;
        switch (candidate.condition)
        {
            case Variant::Condition::Always:
                matches = true;
                break;
            case Variant::Condition::IfArgPresent:
                matches = hasArg(candidate.conditionArg);
                break;
            case Variant::Condition::IfArgMissing:
                matches = !hasArg(candidate.conditionArg);
                break;
            case Variant::Condition::IfArgTypeIs:
                if (hasArg(candidate.conditionArg) && originalType(candidate.conditionArg))
                    matches = *originalType(candidate.conditionArg) == candidate.conditionType;
                break;
            case Variant::Condition::IfArgTypeIsNot:
                if (hasArg(candidate.conditionArg) && originalType(candidate.conditionArg))
                    matches = *originalType(candidate.conditionArg) != candidate.conditionType;
                break;
        }
        if (matches)
        {
            selected = &candidate;
            break;
        }
    }

    if (!selected && !variants.empty())
        selected = &variants.front();
    return selected;
}

void BuiltinLowerContext::applyFeatures(const BuiltinLoweringRule::Variant &variant)
{
    for (const auto &feature : variant.features)
    {
        switch (feature.action)
        {
            case FeatureAction::Request:
                lowerer_->requestHelper(feature.feature);
                break;
            case FeatureAction::Track:
                lowerer_->trackRuntime(feature.feature);
                break;
        }
    }
}

void BuiltinLowerContext::setCurrentLoc(il::support::SourceLoc loc)
{
    lowerer_->curLoc = loc;
}

il::core::Type BuiltinLowerContext::boolType() const
{
    return lowerer_->ilBoolTy();
}

il::core::Value BuiltinLowerContext::emitCall(il::core::Type type,
                                              const char *runtime,
                                              const std::vector<il::core::Value> &args)
{
    return lowerer_->emitCallRet(type, runtime, args);
}

il::core::Value BuiltinLowerContext::emitUnary(il::core::Opcode opcode,
                                               il::core::Type type,
                                               il::core::Value value)
{
    return lowerer_->emitUnary(opcode, type, value);
}

il::core::Value BuiltinLowerContext::emitBinary(il::core::Opcode opcode,
                                                il::core::Type type,
                                                il::core::Value lhs,
                                                il::core::Value rhs)
{
    return lowerer_->emitBinary(opcode, type, lhs, rhs);
}

il::core::Value BuiltinLowerContext::emitLoad(il::core::Type type, il::core::Value addr)
{
    return lowerer_->emitLoad(type, addr);
}

il::core::Value BuiltinLowerContext::emitAlloca(int bytes)
{
    return lowerer_->emitAlloca(bytes);
}

void BuiltinLowerContext::emitCBr(il::core::Value cond,
                                  il::core::BasicBlock *t,
                                  il::core::BasicBlock *f)
{
    lowerer_->emitCBr(cond, t, f);
}

void BuiltinLowerContext::emitTrap()
{
    lowerer_->emitTrap();
}

void BuiltinLowerContext::setCurrentBlock(il::core::BasicBlock *block)
{
    lowerer_->context().setCurrent(block);
}

std::string BuiltinLowerContext::makeBlockLabel(const char *hint)
{
    Lowerer::ProcedureContext &procCtx = lowerer_->context();
    Lowerer::BlockNamer *blockNamer = procCtx.blockNames().namer();
    if (blockNamer)
        return blockNamer->generic(hint);
    return lowerer_->mangler.block(std::string(hint));
}

BuiltinLowerContext::BranchPair BuiltinLowerContext::createGuardBlocks(const char *contHint,
                                                                      const char *trapHint)
{
    BranchPair pair{};
    Lowerer::ProcedureContext &procCtx = lowerer_->context();
    il::core::Function *func = procCtx.function();
    il::core::BasicBlock *origin = procCtx.current();
    if (!func || !origin)
        return pair;

    const std::string originLabel = origin->label;
    const std::string contLabel = makeBlockLabel(contHint);
    const std::string trapLabel = makeBlockLabel(trapHint);

    lowerer_->builder->addBlock(*func, contLabel);
    lowerer_->builder->addBlock(*func, trapLabel);

    const auto findBlock = [&](const std::string &label) {
        auto it = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const il::core::BasicBlock &bb) {
            return bb.label == label;
        });
        assert(it != func->blocks.end());
        return &*it;
    };

    auto originIt = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const il::core::BasicBlock &bb) {
        return bb.label == originLabel;
    });
    assert(originIt != func->blocks.end());
    procCtx.setCurrent(&*originIt);

    pair.cont = findBlock(contLabel);
    pair.trap = findBlock(trapLabel);
    return pair;
}

BuiltinLowerContext::ValBlocks BuiltinLowerContext::createValBlocks()
{
    ValBlocks blocks{};
    Lowerer::ProcedureContext &procCtx = lowerer_->context();
    il::core::Function *func = procCtx.function();
    il::core::BasicBlock *origin = procCtx.current();
    if (!func || !origin)
        return blocks;

    const std::string originLabel = origin->label;
    const std::string contLabel = makeBlockLabel("val_ok");
    const std::string trapLabel = makeBlockLabel("val_fail");
    const std::string nanLabel = makeBlockLabel("val_nan");
    const std::string overflowLabel = makeBlockLabel("val_over");

    lowerer_->builder->addBlock(*func, contLabel);
    lowerer_->builder->addBlock(*func, trapLabel);
    lowerer_->builder->addBlock(*func, nanLabel);
    lowerer_->builder->addBlock(*func, overflowLabel);

    const auto findBlock = [&](const std::string &label) {
        auto it = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const il::core::BasicBlock &bb) {
            return bb.label == label;
        });
        assert(it != func->blocks.end());
        return &*it;
    };

    auto originIt = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const il::core::BasicBlock &bb) {
        return bb.label == originLabel;
    });
    assert(originIt != func->blocks.end());
    procCtx.setCurrent(&*originIt);

    blocks.cont = findBlock(contLabel);
    blocks.trap = findBlock(trapLabel);
    blocks.nan = findBlock(nanLabel);
    blocks.overflow = findBlock(overflowLabel);
    return blocks;
}

void BuiltinLowerContext::emitConversionTrap(il::support::SourceLoc loc)
{
    setCurrentLoc(loc);
    il::core::Value sentinel = lowerer_->emitUnary(Opcode::CastFpToSiRteChk,
                                                   IlType(IlKind::I64),
                                                   Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
    (void)sentinel;
    lowerer_->emitTrap();
}

Lowerer::RVal lowerStringBuiltin(BuiltinLowerContext &ctx)
{
    const auto *stringSpec = builtins::findBuiltin(ctx.info().name);
    if (stringSpec)
    {
        const std::size_t argCount = ctx.call().args.size();
        if (argCount >= static_cast<std::size_t>(stringSpec->minArity) &&
            argCount <= static_cast<std::size_t>(stringSpec->maxArity))
        {
            builtins::LowerCtx strCtx(ctx.lowerer(), ctx.call());
            Value resultValue = stringSpec->fn(strCtx, strCtx.values());
            return {resultValue, strCtx.resultType()};
        }
    }
    return lowerGenericBuiltin(ctx);
}

Lowerer::RVal lowerMathBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

Lowerer::RVal lowerConversionBuiltin(BuiltinLowerContext &ctx)
{
    return lowerConversionBuiltinImpl(ctx);
}

Lowerer::RVal lowerDefaultBuiltin(BuiltinLowerContext &ctx)
{
    return lowerGenericBuiltin(ctx);
}

namespace
{

Lowerer::RVal lowerGenericBuiltin(BuiltinLowerContext &ctx)
{
    const Variant *variant = ctx.selectVariant();
    if (!variant)
        return ctx.makeZeroResult();
    Lowerer::RVal result = emitVariant(ctx, *variant);
    ctx.applyFeatures(*variant);
    return result;
}

Lowerer::RVal lowerConversionBuiltinImpl(BuiltinLowerContext &ctx)
{
    const Variant *variant = ctx.selectVariant();
    if (!variant)
        return ctx.makeZeroResult();

    Lowerer::RVal result = ctx.makeZeroResult();
    switch (ctx.call().builtin)
    {
        case BuiltinCallExpr::Builtin::Cint:
            result = lowerNumericConversion(ctx, *variant, IlType(IlKind::I64), "cint_ok", "cint_trap");
            break;
        case BuiltinCallExpr::Builtin::Clng:
            result = lowerNumericConversion(ctx, *variant, IlType(IlKind::I64), "clng_ok", "clng_trap");
            break;
        case BuiltinCallExpr::Builtin::Csng:
            result = lowerNumericConversion(ctx, *variant, IlType(IlKind::F64), "csng_ok", "csng_trap");
            break;
        case BuiltinCallExpr::Builtin::Val:
            result = lowerValBuiltin(ctx, *variant);
            break;
        default:
            result = emitVariant(ctx, *variant);
            break;
    }

    ctx.applyFeatures(*variant);
    return result;
}

Lowerer::RVal emitVariant(BuiltinLowerContext &ctx, const Variant &variant)
{
    switch (variant.kind)
    {
        case Variant::Kind::CallRuntime:
            return emitCallRuntime(ctx, variant);
        case Variant::Kind::EmitUnary:
            return emitUnary(ctx, variant);
        case Variant::Kind::Custom:
            return emitCustom(ctx, variant);
    }
    return ctx.makeZeroResult();
}

Lowerer::RVal emitCallRuntime(BuiltinLowerContext &ctx, const Variant &variant)
{
    std::vector<Value> callArgs;
    callArgs.reserve(variant.arguments.size());
    for (const auto &argSpec : variant.arguments)
    {
        Lowerer::RVal &argVal = ctx.applyTransforms(argSpec, argSpec.transforms);
        callArgs.push_back(argVal.value);
    }
    IlType resultType = ctx.resolveResultType();
    ctx.setCurrentLoc(ctx.callLoc(variant.callLocArg));
    Value resultValue = ctx.emitCall(resultType, variant.runtime, callArgs);
    return {resultValue, resultType};
}

Lowerer::RVal emitUnary(BuiltinLowerContext &ctx, const Variant &variant)
{
    assert(!variant.arguments.empty() && "unary builtin requires an operand");
    const auto &argSpec = variant.arguments.front();
    Lowerer::RVal &argVal = ctx.applyTransforms(argSpec, argSpec.transforms);
    IlType resultType = ctx.resolveResultType();
    ctx.setCurrentLoc(ctx.callLoc(variant.callLocArg));
    Value resultValue = ctx.emitUnary(variant.opcode, resultType, argVal.value);
    return {resultValue, resultType};
}

Lowerer::RVal emitCustom(BuiltinLowerContext &ctx, const Variant &variant)
{
    switch (ctx.call().builtin)
    {
        case BuiltinCallExpr::Builtin::Cint:
            return lowerNumericConversion(ctx, variant, IlType(IlKind::I64), "cint_ok", "cint_trap");
        case BuiltinCallExpr::Builtin::Clng:
            return lowerNumericConversion(ctx, variant, IlType(IlKind::I64), "clng_ok", "clng_trap");
        case BuiltinCallExpr::Builtin::Csng:
            return lowerNumericConversion(ctx, variant, IlType(IlKind::F64), "csng_ok", "csng_trap");
        case BuiltinCallExpr::Builtin::Val:
            return lowerValBuiltin(ctx, variant);
        default:
            if (auto *diag = ctx.lowerer().diagnosticEmitter())
            {
                ctx.setCurrentLoc(ctx.call().loc);
                diag->emit(il::support::Severity::Error,
                           "B4003",
                           ctx.call().loc,
                           0,
                           "custom builtin lowering variant is not supported");
            }
            return ctx.makeZeroResult();
    }
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

} // namespace

const std::unordered_map<std::string, BuiltinFn> &builtinLowererRegistry()
{
    static const std::unordered_map<std::string, BuiltinFn> kRegistry = [] {
        std::unordered_map<std::string, BuiltinFn> map;
        const auto assign = [&](BuiltinFn fn, std::initializer_list<BuiltinCallExpr::Builtin> builtins) {
            for (auto builtin : builtins)
                map.emplace(getBuiltinInfo(builtin).name, fn);
        };

        assign(&lowerStringBuiltin,
               {BuiltinCallExpr::Builtin::Len,
                BuiltinCallExpr::Builtin::Mid,
                BuiltinCallExpr::Builtin::Left,
                BuiltinCallExpr::Builtin::Right,
                BuiltinCallExpr::Builtin::Str,
                BuiltinCallExpr::Builtin::Instr,
                BuiltinCallExpr::Builtin::Ltrim,
                BuiltinCallExpr::Builtin::Rtrim,
                BuiltinCallExpr::Builtin::Trim,
                BuiltinCallExpr::Builtin::Ucase,
                BuiltinCallExpr::Builtin::Lcase,
                BuiltinCallExpr::Builtin::Chr,
                BuiltinCallExpr::Builtin::Asc,
                BuiltinCallExpr::Builtin::InKey,
                BuiltinCallExpr::Builtin::GetKey});

        assign(&lowerConversionBuiltin,
               {BuiltinCallExpr::Builtin::Val,
                BuiltinCallExpr::Builtin::Cint,
                BuiltinCallExpr::Builtin::Clng,
                BuiltinCallExpr::Builtin::Csng});

        assign(&lowerMathBuiltin,
               {BuiltinCallExpr::Builtin::Cdbl,
                BuiltinCallExpr::Builtin::Int,
                BuiltinCallExpr::Builtin::Fix,
                BuiltinCallExpr::Builtin::Round,
                BuiltinCallExpr::Builtin::Sqr,
                BuiltinCallExpr::Builtin::Abs,
                BuiltinCallExpr::Builtin::Floor,
                BuiltinCallExpr::Builtin::Ceil,
                BuiltinCallExpr::Builtin::Sin,
                BuiltinCallExpr::Builtin::Cos,
                BuiltinCallExpr::Builtin::Pow,
                BuiltinCallExpr::Builtin::Rnd});

        return map;
    }();
    return kRegistry;
}

} // namespace il::frontends::basic::lower

