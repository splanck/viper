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
//   opcode-specific rules, and delegates arithmetic and memory checks to
//   specialised helpers to enforce the IL specification.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Instruction verification dispatcher for the IL verifier.
/// @details Exposes helper functions that validate instruction signatures and
///          semantics using metadata tables and opcode-specific routines.

#include "il/verify/InstructionChecker.hpp"

#include "il/core/Opcode.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/InstructionCheckerShared.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/SpecTables.hpp"
#include "il/verify/TypeInference.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace il::verify
{
namespace
{

using checker::checkAddrOf;
using checker::checkAlloca;
using checker::checkCall;
using checker::checkConstNull;
using checker::checkConstStr;
using checker::checkGEP;
using checker::checkIdxChk;
using checker::checkLoad;
using checker::checkStore;
using checker::checkTrapErr;
using checker::checkTrapFromErr;
using checker::checkTrapKind;
using checker::checkUnary;
using checker::fail;
using il::core::Opcode;
using il::core::Type;
using il::support::Expected;

Expected<void> rejectUnchecked(const VerifyCtx &ctx, std::string_view message)
{
    return fail(ctx, std::string(message));
}

Expected<void> runCastFpToSiRteChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checkUnary(ctx, Type::Kind::F64, ctx.instr.type);
}

Expected<void> runCastFpToUiRteChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checkUnary(ctx, Type::Kind::F64, ctx.instr.type);
}

Expected<void> runCastSiNarrowChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checkUnary(ctx, Type::Kind::I64, ctx.instr.type);
}

Expected<void> runCastUiNarrowChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checkUnary(ctx, Type::Kind::I64, ctx.instr.type);
}

Expected<void> applyResultInference(const VerifyCtx &ctx, const OpcodeSpec &spec)
{
    const auto &instr = ctx.instr;
    if (!instr.result)
        return {};
    if (spec.signature.resultArity == il::core::ResultArity::None)
        return {};

    if (auto type = typeFromTypeClass(spec.signature.resultType))
    {
        ctx.types.recordResult(instr, *type);
        return {};
    }

    if (spec.signature.resultType == TypeClass::InstrType || spec.signature.resultType == TypeClass::None)
        ctx.types.recordResult(instr, instr.type);

    return {};
}

Expected<void> runRule(const VerifyCtx &ctx, const OpcodeSpec &spec, const VerifyRule &rule)
{
    switch (rule.action)
    {
        case VerifyAction::Default:
            return applyResultInference(ctx, spec);
        case VerifyAction::Reject:
            assert(rule.message && "reject action requires diagnostic message");
            return rejectUnchecked(ctx, rule.message);
        case VerifyAction::IdxChk:
            return checkIdxChk(ctx);
        case VerifyAction::Alloca:
            return checkAlloca(ctx);
        case VerifyAction::GEP:
            return checkGEP(ctx);
        case VerifyAction::Load:
            return checkLoad(ctx);
        case VerifyAction::Store:
            return checkStore(ctx);
        case VerifyAction::AddrOf:
            return checkAddrOf(ctx);
        case VerifyAction::ConstStr:
            return checkConstStr(ctx);
        case VerifyAction::ConstNull:
            return checkConstNull(ctx);
        case VerifyAction::Call:
            return checkCall(ctx);
        case VerifyAction::TrapKind:
            return checkTrapKind(ctx);
        case VerifyAction::TrapFromErr:
            return checkTrapFromErr(ctx);
        case VerifyAction::TrapErr:
            return checkTrapErr(ctx);
        case VerifyAction::CastFpToSiRteChk:
            return runCastFpToSiRteChk(ctx);
        case VerifyAction::CastFpToUiRteChk:
            return runCastFpToUiRteChk(ctx);
        case VerifyAction::CastSiNarrowChk:
            return runCastSiNarrowChk(ctx);
        case VerifyAction::CastUiNarrowChk:
            return runCastUiNarrowChk(ctx);
    }

    return applyResultInference(ctx, spec);
}

Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const auto &spec = getOpcodeSpec(ctx.instr.op);

    detail::OperandCountChecker countChecker(ctx, spec.signature);
    if (auto countResult = countChecker.run(); !countResult)
        return countResult;

    detail::OperandTypeChecker typeChecker(ctx, spec.signature);
    if (auto typeResult = typeChecker.run(); !typeResult)
        return typeResult;

    detail::ResultTypeChecker resultChecker(ctx, spec.signature, ctx.instr.op);
    if (auto result = resultChecker.run(); !result)
        return result;

    const auto &rule = getVerifyRule(ctx.instr.op);
    return runRule(ctx, spec, rule);
}

} // namespace

using il::support::makeError;

/// @brief Check an instruction's structural signature against metadata.
/// @details Validates result presence, operand counts, successor arity, and
///          branch argument bundles using schema-derived metadata. Produces
///          targeted diagnostics when the instruction deviates from the
///          specification.
/// @param fn Function providing diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction being validated.
/// @return Empty success or a descriptive diagnostic on failure.
Expected<void> verifyOpcodeSignature_impl(const il::core::Function &fn,
                                          const il::core::BasicBlock &bb,
                                          const il::core::Instr &instr)
{
    const auto &spec = getOpcodeSpec(instr.op);
    const auto &signature = spec.signature;
    const auto &flags = spec.flags;

    const bool hasResult = instr.result.has_value();
    switch (signature.resultArity)
    {
        case il::core::ResultArity::None:
            if (hasResult)
                return Expected<void>(
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unexpected result")));
            break;
        case il::core::ResultArity::One:
            if (!hasResult)
                return Expected<void>(
                    makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing result")));
            break;
        case il::core::ResultArity::Optional:
            break;
    }

    const size_t operandCount = instr.operands.size();
    const bool variadic = signature.operandMax == il::core::kVariadicOperandCount;
    if (operandCount < signature.operandMin || (!variadic && operandCount > signature.operandMax))
    {
        std::string message;
        if (signature.operandMin == signature.operandMax)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(signature.operandMin)) +
                      " operand";
            if (signature.operandMin != 1)
                message += 's';
        }
        else if (variadic)
        {
            message = "expected at least " +
                      std::to_string(static_cast<unsigned>(signature.operandMin)) + " operand";
            if (signature.operandMin != 1)
                message += 's';
        }
        else
        {
            message = "expected between " +
                      std::to_string(static_cast<unsigned>(signature.operandMin)) + " and " +
                      std::to_string(static_cast<unsigned>(signature.operandMax)) + " operands";
        }
        return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
    }

    const bool variadicSucc = flags.successors == il::core::kVariadicOperandCount;
    if (variadicSucc)
    {
        if (instr.labels.empty())
            return Expected<void>(makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != flags.successors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(flags.successors)) +
                                  " successor";
            if (flags.successors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    if (variadicSucc)
    {
        if (!instr.brArgs.empty() && instr.brArgs.size() != instr.labels.size())
            return Expected<void>(makeError(
                instr.loc,
                formatInstrDiag(
                    fn, bb, instr, "expected branch argument bundle per successor or none")));
    }
    else
    {
        if (instr.brArgs.size() > flags.successors)
        {
            std::string message = "expected at most " +
                                  std::to_string(static_cast<unsigned>(flags.successors)) +
                                  " branch argument bundle";
            if (flags.successors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != flags.successors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(flags.successors)) +
                                  " branch argument bundle";
            if (flags.successors != 1)
                message += 's';
            message += ", or none";
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    return {};
}

/// @brief Entry point for verifying an instruction with an existing context.
/// @details Thin wrapper that forwards to @ref verifyInstruction_impl so the
///          exported API can evolve independently of the internal helper.
/// @param ctx Verification context describing the instruction under test.
/// @return Empty success or a diagnostic failure.
Expected<void> verifyInstruction_E(const VerifyCtx &ctx)
{
    return verifyInstruction_impl(ctx);
}

/// @brief Entry point for validating instruction signatures with an existing context.
/// @details Bridges @ref VerifyCtx users to the shared signature checker,
///          ensuring consistent diagnostics for both public and internal callers.
/// @param ctx Verification context describing the instruction under test.
/// @return Empty success or a diagnostic failure.
Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx)
{
    return verifyOpcodeSignature_impl(ctx.fn, ctx.block, ctx.instr);
}

/// @brief Verify an instruction using discrete verifier dependencies.
/// @details Constructs a @ref VerifyCtx from the provided function, block,
///          instruction, external symbol tables, and type inference cache before
///          delegating to @ref verifyInstruction_impl.
/// @param fn Function supplying structural context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction under verification.
/// @param externs Map of extern declarations available to the verifier.
/// @param funcs Map of callable functions referenced by the program.
/// @param types Type inference helper used to query operand types.
/// @param sink Diagnostic sink collecting warnings and errors.
/// @return Empty success or a diagnostic failure.
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

/// @brief Validate an instruction signature without constructing a context manually.
/// @details Convenience overload that forwards to
///          @ref verifyOpcodeSignature_impl while accepting the minimal data
///          required by diagnostics.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose signature is checked.
/// @return Empty success or a diagnostic failure.
Expected<void> verifyOpcodeSignature_E(const il::core::Function &fn,
                                       const il::core::BasicBlock &bb,
                                       const il::core::Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

/// @brief Public API for signature checking that prints diagnostics immediately.
/// @details Invokes @ref verifyOpcodeSignature_E and prints any resulting
///          diagnostics to the supplied stream, returning @c false on failure.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose signature is checked.
/// @param err Stream used for diagnostic emission.
/// @return @c true when verification succeeds, @c false otherwise.
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

/// @brief Public API for full instruction verification that prints diagnostics.
/// @details Runs verification using a collecting sink so warnings and errors can
///          be emitted in order to the provided stream.  Returns @c false when a
///          hard error is encountered.
/// @param fn Function supplying structural context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction under verification.
/// @param externs Map of extern declarations referenced by the module.
/// @param funcs Map of function declarations referenced by the module.
/// @param types Type inference helper used during verification.
/// @param err Stream receiving diagnostics.
/// @return @c true on success, @c false when verification fails.
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

/// @brief Convenience wrapper returning diagnostics through @ref Expected.
/// @details Allows callers that already operate on @ref Expected to reuse the
///          signature checker without dealing with streams.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose signature is checked.
/// @return Empty success or a diagnostic failure.
Expected<void> verifyOpcodeSignature_expected(const il::core::Function &fn,
                                              const il::core::BasicBlock &bb,
                                              const il::core::Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

/// @brief Return instruction verification results using the @ref Expected channel.
/// @details Reuses the richer overload while exposing the result in a form
///          convenient for callers that manage diagnostics themselves.
/// @param fn Function supplying structural context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction under verification.
/// @param externs Map of extern declarations referenced by the module.
/// @param funcs Map of function declarations referenced by the module.
/// @param types Type inference helper used during verification.
/// @param sink Diagnostic sink that receives emitted warnings.
/// @return Empty success or a diagnostic failure.
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
