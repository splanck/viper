//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
// File: src/il/verify/InstructionChecker.cpp
//
// Summary:
//   Implements the central dispatch for verifying individual IL instructions.
//   The checker orchestrates operand count/type validation, applies
//   opcode-specific rules, and delegates to specialised helpers to enforce the
//   IL specification.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Instruction verification dispatcher for the IL verifier.
/// @details Drives verification from schema-derived tables so that operand and
///          result validation, control-flow checks, and opcode-specific behaviour
///          stay synchronised with the shared opcode definition file.

#include "il/verify/InstructionChecker.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/InstructionCheckerShared.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/SpecTables.hpp"
#include "il/verify/TypeInference.hpp"
#include "support/diag_expected.hpp"

#include <array>
#include <string>
#include <unordered_map>

namespace il::verify
{
namespace
{

using checker::checkAddrOf;
using checker::checkAlloca;
using checker::checkCall;
using checker::checkConstNull;
using checker::checkConstStr;
using checker::checkDefault;
using checker::checkGEP;
using checker::checkIdxChk;
using checker::checkLoad;
using checker::checkStore;
using checker::checkTrapErr;
using checker::checkTrapFromErr;
using checker::checkTrapKind;
using checker::fail;
using il::core::Type;
using il::support::Expected;
using il::support::makeError;

using StrategyFn = Expected<void> (*)(const VerifyCtx &, const InstructionSpec &);

Expected<void> applyDefault(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkDefault(ctx);
}

Expected<void> applyAlloca(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkAlloca(ctx);
}

Expected<void> applyGEP(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkGEP(ctx);
}

Expected<void> applyLoad(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkLoad(ctx);
}

Expected<void> applyStore(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkStore(ctx);
}

Expected<void> applyAddrOf(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkAddrOf(ctx);
}

Expected<void> applyConstStr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkConstStr(ctx);
}

Expected<void> applyConstNull(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkConstNull(ctx);
}

Expected<void> applyCall(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkCall(ctx);
}

Expected<void> applyTrapKind(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapKind(ctx);
}

Expected<void> applyTrapFromErr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapFromErr(ctx);
}

Expected<void> applyTrapErr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapErr(ctx);
}

Expected<void> applyIdxChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkIdxChk(ctx);
}

Expected<void> applyCastFpToSiRteChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32 && kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

Expected<void> applyCastFpToUiRteChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32 && kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

Expected<void> applyCastSiNarrowChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

Expected<void> applyCastUiNarrowChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

Expected<void> applyReject(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    const char *message = spec.rejectMessage ? spec.rejectMessage : "opcode rejected";
    return fail(ctx, std::string(message));
}

constexpr std::array<StrategyFn, static_cast<size_t>(VerifyStrategy::Count)> kStrategyTable = {
    &applyDefault,
    &applyAlloca,
    &applyGEP,
    &applyLoad,
    &applyStore,
    &applyAddrOf,
    &applyConstStr,
    &applyConstNull,
    &applyCall,
    &applyTrapKind,
    &applyTrapFromErr,
    &applyTrapErr,
    &applyIdxChk,
    &applyCastFpToSiRteChk,
    &applyCastFpToUiRteChk,
    &applyCastSiNarrowChk,
    &applyCastUiNarrowChk,
    &applyReject,
};

static_assert(kStrategyTable.size() == static_cast<size_t>(VerifyStrategy::Count),
              "strategy table must cover every enumerator");

Expected<void> dispatchStrategy(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    const size_t index = static_cast<size_t>(spec.strategy);
    if (index >= kStrategyTable.size())
        return checkDefault(ctx);
    return kStrategyTable[index](ctx, spec);
}

Expected<void> runStructuralChecks(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    detail::OperandCountChecker countChecker(ctx, spec);
    if (auto result = countChecker.run(); !result)
        return result;

    detail::OperandTypeChecker typeChecker(ctx, spec);
    if (auto result = typeChecker.run(); !result)
        return result;

    detail::ResultTypeChecker resultChecker(ctx, spec);
    return resultChecker.run();
}

Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const InstructionSpec &spec = getInstructionSpec(ctx.instr.op);
    if (auto result = runStructuralChecks(ctx, spec); !result)
        return result;
    return dispatchStrategy(ctx, spec);
}

Expected<void> verifyOpcodeSignature_impl(const il::core::Function &fn,
                                          const il::core::BasicBlock &bb,
                                          const il::core::Instr &instr)
{
    const InstructionSpec &spec = getInstructionSpec(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (spec.resultArity)
    {
        case il::core::ResultArity::None:
            if (hasResult)
                return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unexpected result")));
            break;
        case il::core::ResultArity::One:
            if (!hasResult)
                return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing result")));
            break;
        case il::core::ResultArity::Optional:
            break;
    }

    const size_t operandCount = instr.operands.size();
    const bool variadicOperands = il::core::isVariadicOperandCount(spec.numOperandsMax);
    if (operandCount < spec.numOperandsMin ||
        (!variadicOperands && operandCount > spec.numOperandsMax))
    {
        std::string message;
        if (spec.numOperandsMin == spec.numOperandsMax && !variadicOperands)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(spec.numOperandsMin)) + " operand";
            if (spec.numOperandsMin != 1)
                message += 's';
        }
        else if (variadicOperands)
        {
            message = "expected at least " + std::to_string(static_cast<unsigned>(spec.numOperandsMin)) + " operand";
            if (spec.numOperandsMin != 1)
                message += 's';
        }
        else
        {
            message = "expected between " + std::to_string(static_cast<unsigned>(spec.numOperandsMin)) + " and " +
                      std::to_string(static_cast<unsigned>(spec.numOperandsMax)) + " operands";
        }
        return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
    }

    const bool variadicSucc = il::core::isVariadicSuccessorCount(spec.numSuccessors);
    if (variadicSucc)
    {
        if (instr.labels.empty())
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != spec.numSuccessors)
        {
            std::string message = "expected " + std::to_string(static_cast<unsigned>(spec.numSuccessors)) + " successor";
            if (spec.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    if (variadicSucc)
    {
        if (!instr.brArgs.empty() && instr.brArgs.size() != instr.labels.size())
        {
            return Expected<void>(makeError(instr.loc,
                                            formatInstrDiag(
                                                fn, bb, instr, "expected branch argument bundle per successor or none")));
        }
    }
    else
    {
        if (instr.brArgs.size() > spec.numSuccessors)
        {
            std::string message = "expected at most " + std::to_string(static_cast<unsigned>(spec.numSuccessors)) +
                                  " branch argument bundle";
            if (spec.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != spec.numSuccessors)
        {
            std::string message = "expected " + std::to_string(static_cast<unsigned>(spec.numSuccessors)) +
                                  " branch argument bundle";
            if (spec.numSuccessors != 1)
                message += 's';
            message += ", or none";
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    return {};
}

} // namespace

Expected<void> verifyInstruction_E(const VerifyCtx &ctx)
{
    return verifyInstruction_impl(ctx);
}

Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx)
{
    return verifyOpcodeSignature_impl(ctx.fn, ctx.block, ctx.instr);
}

Expected<void> verifyInstruction_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::Extern *> &externs,
    const std::unordered_map<std::string, const il::core::Function *> &funcs,
    TypeInference &types,
    DiagSink &sink)
{
    VerifyCtx ctx{sink, types, externs, funcs, fn, bb, instr};
    return verifyInstruction_impl(ctx);
}

Expected<void> verifyOpcodeSignature_E(const il::core::Function &fn,
                                       const il::core::BasicBlock &bb,
                                       const il::core::Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

bool verifyOpcodeSignature(const il::core::Function &fn,
                           const il::core::BasicBlock &bb,
                           const il::core::Instr &instr,
                           std::ostream &err)
{
    if (auto result = verifyOpcodeSignature_E(fn, bb, instr); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyInstruction(const il::core::Function &fn,
                       const il::core::BasicBlock &bb,
                       const il::core::Instr &instr,
                       const std::unordered_map<std::string, const il::core::Extern *> &externs,
                       const std::unordered_map<std::string, const il::core::Function *> &funcs,
                       TypeInference &types,
                       std::ostream &err)
{
    CollectingDiagSink sink;
    if (auto result = verifyInstruction_E(fn, bb, instr, externs, funcs, types, sink); !result)
    {
        for (const auto &warning : sink.diagnostics())
            il::support::printDiag(warning, err);
        il::support::printDiag(result.error(), err);
        return false;
    }

    for (const auto &warning : sink.diagnostics())
        il::support::printDiag(warning, err);
    return true;
}

Expected<void> verifyOpcodeSignature_expected(const il::core::Function &fn,
                                              const il::core::BasicBlock &bb,
                                              const il::core::Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

Expected<void> verifyInstruction_expected(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::Extern *> &externs,
    const std::unordered_map<std::string, const il::core::Function *> &funcs,
    TypeInference &types,
    DiagSink &sink)
{
    return verifyInstruction_E(fn, bb, instr, externs, funcs, types, sink);
}

} // namespace il::verify

