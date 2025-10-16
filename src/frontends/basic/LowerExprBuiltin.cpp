// File: src/frontends/basic/LowerExprBuiltin.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements builtin expression lowering helpers for the BASIC Lowerer.
// Key invariants: Builtin lowering preserves runtime tracking and shared
//                 coercion logic provided by the Lowerer core.
// Ownership/Lifetime: Helpers borrow the Lowerer reference for individual
//                      builtin invocations.
// Links: docs/codemap.md

#include "frontends/basic/LowerExprBuiltin.hpp"

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/builtins/StringBuiltins.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace il::frontends::basic
{
using il::core::BasicBlock;
using il::core::Function;
using il::core::Opcode;
using il::core::Value;
using IlType = il::core::Type;
using IlKind = IlType::Kind;
using ExprType = Lowerer::ExprType;

namespace
{
constexpr std::string_view kDiagUnsupportedCustomBuiltinVariant = "B4003";
} // namespace

BuiltinExprLowering::BuiltinExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

Lowerer::RVal BuiltinExprLowering::lower(const BuiltinCallExpr &expr)
{
    Lowerer &self = *lowerer_;
    const auto &call = expr;
    const auto &info = getBuiltinInfo(call.builtin);
    if (const auto *stringSpec = builtins::findBuiltin(info.name))
    {
        const std::size_t argCount = call.args.size();
        if (argCount >= static_cast<std::size_t>(stringSpec->minArity) &&
            argCount <= static_cast<std::size_t>(stringSpec->maxArity))
        {
            builtins::LowerCtx ctx(self, call);
            Value resultValue = stringSpec->fn(ctx, ctx.values());
            return {resultValue, ctx.resultType()};
        }
    }

    if (call.builtin == BuiltinCallExpr::Builtin::Lof)
    {
        self.requireLofCh();
        if (call.args.empty() || !call.args[0])
            return {Value::constInt(0), IlType(IlKind::I64)};

        Lowerer::RVal channel = self.lowerExpr(*call.args[0]);
        channel = self.normalizeChannelToI32(std::move(channel), call.loc);

        self.curLoc = call.loc;
        Value raw = self.emitCallRet(IlType(IlKind::I64), "rt_lof_ch", {channel.value});

        Value isError = self.emitBinary(Opcode::SCmpLT, self.ilBoolTy(), raw, Value::constInt(0));

        Lowerer::ProcedureContext &ctx = self.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string failLbl =
                blockNamer ? blockNamer->generic("lof_err") : self.mangler.block("lof_err");
            std::string contLbl =
                blockNamer ? blockNamer->generic("lof_cont") : self.mangler.block("lof_cont");

            std::size_t failIdx = func->blocks.size();
            self.builder->addBlock(*func, failLbl);
            std::size_t contIdx = func->blocks.size();
            self.builder->addBlock(*func, contLbl);

            BasicBlock *failBlk = &func->blocks[failIdx];
            BasicBlock *contBlk = &func->blocks[contIdx];

            ctx.setCurrent(&func->blocks[originIdx]);
            self.curLoc = call.loc;
            self.emitCBr(isError, failBlk, contBlk);

            ctx.setCurrent(failBlk);
            self.curLoc = call.loc;
            Value negCode =
                self.emitBinary(Opcode::Sub, IlType(IlKind::I64), Value::constInt(0), raw);
            Value err32 = self.emitUnary(Opcode::CastSiNarrowChk, IlType(IlKind::I32), negCode);
            self.emitTrapFromErr(err32);

            ctx.setCurrent(contBlk);
        }

        self.curLoc = call.loc;
        return {raw, IlType(IlKind::I64)};
    }
    if (call.builtin == BuiltinCallExpr::Builtin::Eof)
    {
        self.requireEofCh();
        if (call.args.empty() || !call.args[0])
            return {Value::constInt(0), IlType(IlKind::I64)};

        Lowerer::RVal channel = self.lowerExpr(*call.args[0]);
        channel = self.normalizeChannelToI32(std::move(channel), call.loc);

        self.curLoc = call.loc;
        Value raw = self.emitCallRet(IlType(IlKind::I32), "rt_eof_ch", {channel.value});

        Lowerer::ProcedureContext &ctx = self.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string failLbl =
                blockNamer ? blockNamer->generic("eof_err") : self.mangler.block("eof_err");
            std::string contLbl =
                blockNamer ? blockNamer->generic("eof_cont") : self.mangler.block("eof_cont");

            std::size_t failIdx = func->blocks.size();
            self.builder->addBlock(*func, failLbl);
            std::size_t contIdx = func->blocks.size();
            self.builder->addBlock(*func, contLbl);

            BasicBlock *failBlk = &func->blocks[failIdx];
            BasicBlock *contBlk = &func->blocks[contIdx];

            ctx.setCurrent(&func->blocks[originIdx]);
            self.curLoc = call.loc;
            Value zero =
                self.emitUnary(Opcode::CastSiNarrowChk, IlType(IlKind::I32), Value::constInt(0));
            Value negOne =
                self.emitUnary(Opcode::CastSiNarrowChk, IlType(IlKind::I32), Value::constInt(-1));
            Value nonZero = self.emitBinary(Opcode::ICmpNe, self.ilBoolTy(), raw, zero);
            Value notNegOne = self.emitBinary(Opcode::ICmpNe, self.ilBoolTy(), raw, negOne);
            Value isError = self.emitBinary(Opcode::And, self.ilBoolTy(), nonZero, notNegOne);
            self.emitCBr(isError, failBlk, contBlk);

            ctx.setCurrent(failBlk);
            self.curLoc = call.loc;
            self.emitTrapFromErr(raw);

            ctx.setCurrent(contBlk);
        }

        self.curLoc = call.loc;
        Lowerer::RVal widened{raw, IlType(IlKind::I32)};
        widened = self.ensureI64(std::move(widened), call.loc);
        return widened;
    }
    if (call.builtin == BuiltinCallExpr::Builtin::Loc)
    {
        self.requireLocCh();
        if (call.args.empty() || !call.args[0])
            return {Value::constInt(0), IlType(IlKind::I64)};

        Lowerer::RVal channel = self.lowerExpr(*call.args[0]);
        channel = self.normalizeChannelToI32(std::move(channel), call.loc);

        self.curLoc = call.loc;
        Value raw = self.emitCallRet(IlType(IlKind::I64), "rt_loc_ch", {channel.value});

        Value isError = self.emitBinary(Opcode::SCmpLT, self.ilBoolTy(), raw, Value::constInt(0));

        Lowerer::ProcedureContext &ctx = self.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string failLbl =
                blockNamer ? blockNamer->generic("loc_err") : self.mangler.block("loc_err");
            std::string contLbl =
                blockNamer ? blockNamer->generic("loc_cont") : self.mangler.block("loc_cont");

            std::size_t failIdx = func->blocks.size();
            self.builder->addBlock(*func, failLbl);
            std::size_t contIdx = func->blocks.size();
            self.builder->addBlock(*func, contLbl);

            BasicBlock *failBlk = &func->blocks[failIdx];
            BasicBlock *contBlk = &func->blocks[contIdx];

            ctx.setCurrent(&func->blocks[originIdx]);
            self.curLoc = call.loc;
            self.emitCBr(isError, failBlk, contBlk);

            ctx.setCurrent(failBlk);
            self.curLoc = call.loc;
            Value negCode =
                self.emitBinary(Opcode::Sub, IlType(IlKind::I64), Value::constInt(0), raw);
            Value err32 = self.emitUnary(Opcode::CastSiNarrowChk, IlType(IlKind::I32), negCode);
            self.emitTrapFromErr(err32);

            ctx.setCurrent(contBlk);
        }

        self.curLoc = call.loc;
        return {raw, IlType(IlKind::I64)};
    }

    const auto &rule = getBuiltinLoweringRule(call.builtin);

    std::vector<std::optional<ExprType>> originalTypes(call.args.size());
    std::vector<std::optional<il::support::SourceLoc>> argLocs(call.args.size());
    for (std::size_t i = 0; i < call.args.size(); ++i)
    {
        const auto &arg = call.args[i];
        if (!arg)
            continue;
        argLocs[i] = arg->loc;
        originalTypes[i] = self.scanExpr(*arg);
    }

    auto hasArg = [&](std::size_t idx) -> bool
    { return idx < call.args.size() && call.args[idx] != nullptr; };

    std::vector<std::optional<Lowerer::RVal>> loweredArgs(call.args.size());
    std::vector<Lowerer::RVal> syntheticArgs;

    const auto *variant = static_cast<const BuiltinLoweringRule::Variant *>(nullptr);
    for (const auto &candidate : rule.variants)
    {
        bool matches = false;
        switch (candidate.condition)
        {
            case BuiltinLoweringRule::Variant::Condition::Always:
                matches = true;
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgPresent:
                matches = hasArg(candidate.conditionArg);
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgMissing:
                matches = !hasArg(candidate.conditionArg);
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgTypeIs:
                if (hasArg(candidate.conditionArg) && originalTypes[candidate.conditionArg])
                    matches = *originalTypes[candidate.conditionArg] == candidate.conditionType;
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgTypeIsNot:
                if (hasArg(candidate.conditionArg) && originalTypes[candidate.conditionArg])
                    matches = *originalTypes[candidate.conditionArg] != candidate.conditionType;
                break;
        }
        if (matches)
        {
            variant = &candidate;
            break;
        }
    }

    if (!variant && !rule.variants.empty())
        variant = &rule.variants.front();
    if (!variant)
        return {Value::constInt(0), IlType(IlKind::I64)};

    auto ensureLoweredIndex = [&](std::size_t idx) -> Lowerer::RVal &
    {
        assert(hasArg(idx) && "builtin lowering referenced missing argument");
        auto &slot = loweredArgs[idx];
        if (!slot)
            slot = self.lowerExpr(*call.args[idx]);
        return *slot;
    };

    auto typeFromExpr = [&](ExprType exprType) -> IlType
    {
        switch (exprType)
        {
            case ExprType::F64:
                return IlType(IlKind::F64);
            case ExprType::Str:
                return IlType(IlKind::Str);
            case ExprType::Bool:
                return self.ilBoolTy();
            case ExprType::I64:
            default:
                return IlType(IlKind::I64);
        }
    };

    auto resolveResultType = [&]() -> IlType
    {
        switch (rule.result.kind)
        {
            case BuiltinLoweringRule::ResultSpec::Kind::Fixed:
                return typeFromExpr(rule.result.type);
            case BuiltinLoweringRule::ResultSpec::Kind::FromArg:
            {
                std::size_t idx = rule.result.argIndex;
                if (hasArg(idx))
                    return ensureLoweredIndex(idx).type;
                return typeFromExpr(rule.result.type);
            }
        }
        return IlType(IlKind::I64);
    };

    auto ensureLoweredArgument =
        [&](const BuiltinLoweringRule::Argument &argSpec) -> Lowerer::RVal &
    {
        std::size_t idx = argSpec.index;
        if (idx < call.args.size() && call.args[idx])
            return ensureLoweredIndex(idx);
        if (argSpec.defaultValue)
        {
            const auto &def = *argSpec.defaultValue;
            Lowerer::RVal value{Value::constInt(def.i64), IlType(IlKind::I64)};
            switch (def.type)
            {
                case ExprType::F64:
                    value = Lowerer::RVal{Value::constFloat(def.f64), IlType(IlKind::F64)};
                    break;
                case ExprType::Str:
                    assert(false && "string default values are not supported");
                    break;
                case ExprType::Bool:
                    value = Lowerer::RVal{self.emitBoolConst(def.i64 != 0), self.ilBoolTy()};
                    break;
                case ExprType::I64:
                default:
                    value = Lowerer::RVal{Value::constInt(def.i64), IlType(IlKind::I64)};
                    break;
            }
            syntheticArgs.push_back(value);
            return syntheticArgs.back();
        }
        assert(false && "builtin lowering referenced missing argument without default");
        syntheticArgs.emplace_back(Value::constInt(0), IlType(IlKind::I64));
        return syntheticArgs.back();
    };

    auto selectArgLoc = [&](const BuiltinLoweringRule::Argument &argSpec) -> il::support::SourceLoc
    {
        if (argSpec.index < argLocs.size() && argLocs[argSpec.index])
            return *argLocs[argSpec.index];
        return call.loc;
    };

    auto applyTransforms =
        [&](const BuiltinLoweringRule::Argument &argSpec,
            const std::vector<BuiltinLoweringRule::ArgTransform> &transforms) -> Lowerer::RVal &
    {
        Lowerer::RVal &slot = ensureLoweredArgument(argSpec);
        il::support::SourceLoc loc = selectArgLoc(argSpec);
        for (const auto &transform : transforms)
        {
            switch (transform.kind)
            {
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureI64:
                    slot = self.ensureI64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureF64:
                    slot = self.ensureF64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureI32:
                    slot = self.ensureI64(std::move(slot), loc);
                    if (slot.type.kind != IlKind::I32)
                    {
                        self.curLoc = loc;
                        slot.value = self.emitUnary(
                            Opcode::CastSiNarrowChk, IlType(IlKind::I32), slot.value);
                        slot.type = IlType(IlKind::I32);
                    }
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceI64:
                    slot = self.coerceToI64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceF64:
                    slot = self.coerceToF64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceBool:
                    slot = self.coerceToBool(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::AddConst:
                    self.curLoc = loc;
                    slot.value = self.emitBinary(Opcode::IAddOvf,
                                                 IlType(IlKind::I64),
                                                 slot.value,
                                                 Value::constInt(transform.immediate));
                    slot.type = IlType(IlKind::I64);
                    break;
            }
        }
        return slot;
    };

    auto selectCallLoc = [&](const std::optional<std::size_t> &idx) -> il::support::SourceLoc
    {
        if (idx && *idx < argLocs.size() && argLocs[*idx])
        {
            return *argLocs[*idx];
        }
        return call.loc;
    };

    Value resultValue = Value::constInt(0);
    IlType resultType = IlType(IlKind::I64);

    switch (variant->kind)
    {
        case BuiltinLoweringRule::Variant::Kind::CallRuntime:
        {
            std::vector<Value> callArgs;
            callArgs.reserve(variant->arguments.size());
            for (const auto &argSpec : variant->arguments)
            {
                Lowerer::RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
                callArgs.push_back(argVal.value);
            }
            resultType = resolveResultType();
            self.curLoc = selectCallLoc(variant->callLocArg);
            resultValue = self.emitCallRet(resultType, variant->runtime, callArgs);
            break;
        }
        case BuiltinLoweringRule::Variant::Kind::EmitUnary:
        {
            assert(!variant->arguments.empty() && "unary builtin requires an operand");
            const auto &argSpec = variant->arguments.front();
            Lowerer::RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
            resultType = resolveResultType();
            self.curLoc = selectCallLoc(variant->callLocArg);
            resultValue = self.emitUnary(variant->opcode, resultType, argVal.value);
            break;
        }
        case BuiltinLoweringRule::Variant::Kind::Custom:
        {
            assert(!variant->arguments.empty() && "custom builtin requires an operand");
            const auto &argSpec = variant->arguments.front();
            Lowerer::RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
            il::support::SourceLoc callLoc = selectCallLoc(variant->callLocArg);

            auto handleConversion = [&](IlType resultTy)
            {
                Value okSlot = self.emitAlloca(1);
                std::vector<Value> callArgs{argVal.value, okSlot};
                resultType = resultTy;
                self.curLoc = callLoc;
                Value callRes = self.emitCallRet(resultType, variant->runtime, callArgs);

                self.curLoc = callLoc;
                Value okVal = self.emitLoad(self.ilBoolTy(), okSlot);
                Lowerer::ProcedureContext &ctx = self.context();
                Function *func = ctx.function();
                assert(func && "conversion lowering requires active function");
                BasicBlock *origin = ctx.current();
                assert(origin && "conversion lowering requires active block");
                std::string originLabel = origin->label;
                Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();
                std::string contLabel =
                    blockNamer ? blockNamer->generic("conv_ok") : self.mangler.block("conv_ok");
                std::string trapLabel =
                    blockNamer ? blockNamer->generic("conv_trap") : self.mangler.block("conv_trap");
                BasicBlock *contBlk = &self.builder->addBlock(*func, contLabel);
                BasicBlock *trapBlk = &self.builder->addBlock(*func, trapLabel);
                auto originIt =
                    std::find_if(func->blocks.begin(),
                                 func->blocks.end(),
                                 [&](const BasicBlock &bb) { return bb.label == originLabel; });
                assert(originIt != func->blocks.end());
                origin = &*originIt;
                ctx.setCurrent(origin);
                self.emitCBr(okVal, contBlk, trapBlk);

                ctx.setCurrent(trapBlk);
                self.curLoc = callLoc;
                Value sentinel =
                    self.emitUnary(Opcode::CastFpToSiRteChk,
                                   IlType(IlKind::I64),
                                   Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
                (void)sentinel;
                self.emitTrap();

                ctx.setCurrent(contBlk);
                resultValue = callRes;
            };

            switch (call.builtin)
            {
                case BuiltinCallExpr::Builtin::Cint:
                    handleConversion(IlType(IlKind::I64));
                    break;
                case BuiltinCallExpr::Builtin::Clng:
                    handleConversion(IlType(IlKind::I64));
                    break;
                case BuiltinCallExpr::Builtin::Csng:
                    handleConversion(IlType(IlKind::F64));
                    break;
                case BuiltinCallExpr::Builtin::Val:
                {
                    il::support::SourceLoc conversionLoc = selectCallLoc(variant->callLocArg);
                    self.curLoc = conversionLoc;
                    Value cstr =
                        self.emitCallRet(IlType(IlKind::Ptr), "rt_string_cstr", {argVal.value});

                    Value okSlot = self.emitAlloca(1);
                    std::vector<Value> callArgs{cstr, okSlot};
                    resultType = resolveResultType();
                    self.curLoc = conversionLoc;
                    Value callRes = self.emitCallRet(resultType, variant->runtime, callArgs);

                    self.curLoc = conversionLoc;
                    Value okVal = self.emitLoad(self.ilBoolTy(), okSlot);

                    Lowerer::ProcedureContext &ctx = self.context();
                    Function *func = ctx.function();
                    assert(func && "VAL lowering requires active function");
                    BasicBlock *origin = ctx.current();
                    assert(origin && "VAL lowering requires active block");
                    std::string originLabel = origin->label;
                    Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();
                    auto labelFor = [&](const char *hint)
                    {
                        if (blockNamer)
                            return blockNamer->generic(hint);
                        return self.mangler.block(std::string(hint));
                    };
                    std::string contLabel = labelFor("val_ok");
                    std::string trapLabel = labelFor("val_fail");
                    std::string nanLabel = labelFor("val_nan");
                    std::string overflowLabel = labelFor("val_over");
                    self.builder->addBlock(*func, contLabel);
                    self.builder->addBlock(*func, trapLabel);
                    self.builder->addBlock(*func, nanLabel);
                    self.builder->addBlock(*func, overflowLabel);

                    auto originIt =
                        std::find_if(func->blocks.begin(),
                                     func->blocks.end(),
                                     [&](const BasicBlock &bb) { return bb.label == originLabel; });
                    assert(originIt != func->blocks.end());
                    origin = &*originIt;
                    auto findBlock = [&](const std::string &label)
                    {
                        auto it =
                            std::find_if(func->blocks.begin(),
                                         func->blocks.end(),
                                         [&](const BasicBlock &bb) { return bb.label == label; });
                        assert(it != func->blocks.end());
                        return &*it;
                    };
                    BasicBlock *contBlk = findBlock(contLabel);
                    BasicBlock *trapBlk = findBlock(trapLabel);
                    BasicBlock *nanBlk = findBlock(nanLabel);
                    BasicBlock *overflowBlk = findBlock(overflowLabel);
                    ctx.setCurrent(origin);
                    self.curLoc = conversionLoc;
                    self.emitCBr(okVal, contBlk, trapBlk);

                    ctx.setCurrent(trapBlk);
                    self.curLoc = conversionLoc;
                    Value isNan =
                        self.emitBinary(Opcode::FCmpNE, self.ilBoolTy(), callRes, callRes);
                    self.emitCBr(isNan, nanBlk, overflowBlk);

                    ctx.setCurrent(nanBlk);
                    self.curLoc = conversionLoc;
                    Value invalidSentinel =
                        self.emitUnary(Opcode::CastFpToSiRteChk,
                                       IlType(IlKind::I64),
                                       Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
                    (void)invalidSentinel;
                    self.emitTrap();

                    ctx.setCurrent(overflowBlk);
                    self.curLoc = conversionLoc;
                    Value overflowSentinel =
                        self.emitUnary(Opcode::CastFpToSiRteChk,
                                       IlType(IlKind::I64),
                                       Value::constFloat(std::numeric_limits<double>::max()));
                    (void)overflowSentinel;
                    self.emitTrap();

                    ctx.setCurrent(contBlk);
                    resultValue = callRes;
                    break;
                }
                default:
                    assert(false && "unsupported custom builtin conversion");
                    return {Value::constInt(0), IlType(IlKind::I64)};
            }
            break;
        }
        default:
        {
            auto variantKindName = [&]() -> std::string
            {
                switch (variant->kind)
                {
                    case BuiltinLoweringRule::Variant::Kind::CallRuntime:
                        return "CallRuntime";
                    case BuiltinLoweringRule::Variant::Kind::EmitUnary:
                        return "EmitUnary";
                    case BuiltinLoweringRule::Variant::Kind::Custom:
                        return "Custom";
                }
                std::string unknown = "<unknown (";
                unknown.append(std::to_string(static_cast<int>(variant->kind)));
                unknown.push_back(')');
                return unknown;
            };

            if (auto *emitter = self.diagnosticEmitter())
            {
                std::string message = "custom builtin lowering variant is not supported: ";
                message.append(variantKindName());
                self.curLoc = selectCallLoc(variant->callLocArg);
                emitter->emit(il::support::Severity::Error,
                              std::string(kDiagUnsupportedCustomBuiltinVariant),
                              self.curLoc,
                              0,
                              std::move(message));
            }

            resultValue = Value::constInt(0);
            resultType = IlType(IlKind::I64);
            break;
        }
    }

    for (const auto &feature : variant->features)
    {
        switch (feature.action)
        {
            case BuiltinLoweringRule::Feature::Action::Request:
                self.requestHelper(feature.feature);
                break;
            case BuiltinLoweringRule::Feature::Action::Track:
                self.trackRuntime(feature.feature);
                break;
        }
    }

    return {resultValue, resultType};
}

Lowerer::RVal Lowerer::lowerBuiltinCall(const BuiltinCallExpr &expr)
{
    BuiltinExprLowering lowering(*this);
    return lowering.lower(expr);
}

Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &expr)
{
    BuiltinExprLowering lowering(lowerer);
    return lowering.lower(expr);
}

} // namespace il::frontends::basic
