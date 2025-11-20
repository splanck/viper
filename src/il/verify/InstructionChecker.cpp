//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/il/verify/InstructionChecker.cpp
// Purpose: Provide the central dispatch for verifying IL instructions against
//          schema-derived opcode metadata.
// Key invariants: Verification must align with /docs/il-guide.md#reference and
//                 report structured diagnostics without mutating the IR. The
//                 dispatcher relies on generated tables for operand counts and
//                 types and defers specialised checks to dedicated helpers.
// Ownership/Lifetime: Operates on caller-owned verification contexts and
//                     retains no global state beyond table references.
// Links: docs/il-guide.md#reference, docs/codemap.md
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
#include "il/verify/InstructionCheckUtils.hpp"
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

/// @brief Derive the result type that should be recorded for an instruction.
/// @details Consults the generated specification to determine whether the
///          opcode yields its declared type, inherits the instruction's explicit
///          type annotation, or produces a concrete kind.  Unknown categories
///          fall back to the instruction's annotated type so diagnostics remain
///          consistent.
/// @param ctx Verification context describing the instruction under review.
/// @param spec Specification entry obtained from the generated opcode table.
/// @return The type that should be recorded for downstream inference.
Type resolveResultType(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    using il::core::TypeCategory;

    switch (spec.resultType)
    {
        case TypeCategory::None:
        case TypeCategory::Void:
        case TypeCategory::Any:
        case TypeCategory::Dynamic:
        case TypeCategory::InstrType:
            return ctx.instr.type;
        default:
            if (auto expectedKind = detail::kindFromCategory(spec.resultType))
                return Type(*expectedKind);
            return ctx.instr.type;
    }
}

/// @brief Execute the default verification strategy.
/// @details Records the instruction's result type using
///          @ref resolveResultType and delegates the structural checks to the
///          shared @ref checker::checkDefault helper.
/// @param ctx Verification context describing the instruction.
/// @param spec Specification entry driving strategy selection.
/// @return Empty on success or a diagnostic-filled error on failure.
Expected<void> applyDefault(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    ctx.types.recordResult(ctx.instr, resolveResultType(ctx, spec));
    return checkDefault(ctx);
}

/// @brief Validate semantics specific to @c alloca instructions.
/// @details Delegates to @ref checker::checkAlloca which enforces pointer type
///          constraints and stack lifetime rules.
/// @param ctx Verification context for the instruction.
/// @return Verification success or failure.
Expected<void> applyAlloca(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkAlloca(ctx);
}

/// @brief Validate @c gep (get-element-pointer) instructions.
/// @details Ensures indices and base pointers satisfy the layout rules captured
///          by the verifier helpers.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyGEP(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkGEP(ctx);
}

/// @brief Validate @c load instructions.
/// @details Enforces addressability and type constraints via the shared
///          @ref checker::checkLoad routine.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyLoad(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkLoad(ctx);
}

/// @brief Validate @c store instructions.
/// @details Confirms pointer types, writeability, and operand arity using the
///          shared checker helper.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyStore(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkStore(ctx);
}

/// @brief Validate @c addr_of instructions.
/// @details Ensures the operand references an addressable entity, deferring to
///          @ref checker::checkAddrOf for detailed diagnostics.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyAddrOf(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkAddrOf(ctx);
}

/// @brief Validate @c const_str instructions.
/// @details Confirms literal encoding and type annotations through the shared
///          checker helper.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyConstStr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkConstStr(ctx);
}

/// @brief Validate @c const_null instructions.
/// @details Delegates to the shared checker to enforce pointer typing rules.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyConstNull(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkConstNull(ctx);
}

/// @brief Validate @c call instructions.
/// @details Leverages @ref checker::checkCall to resolve callee signatures and
///          enforce argument compatibility.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyCall(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkCall(ctx);
}

/// @brief Validate trap-kind instructions.
/// @details Ensures trap codes and argument payloads align with the runtime
///          specification.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyTrapKind(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapKind(ctx);
}

/// @brief Validate @c trap_from_error instructions.
/// @details Delegates to the shared checker to confirm operand layout and error
///          codes.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyTrapFromErr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapFromErr(ctx);
}

/// @brief Validate @c trap_error instructions.
/// @details Confirms operand arity and message payload rules through the shared
///          helper.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyTrapErr(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkTrapErr(ctx);
}

/// @brief Validate index-check instructions emitted for bounds checking.
/// @details Delegates to @ref checker::checkIdxChk to ensure operand ordering
///          and type annotations follow the runtime contract.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyIdxChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    return checkIdxChk(ctx);
}

/// @brief Validate floating-to-signed casts with round-to-even semantics.
/// @details Checks that the result type is one of the supported signed integer
///          widths before recording the type for downstream inference.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyCastFpToSiRteChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32 && kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

/// @brief Validate floating-to-unsigned casts with round-to-even semantics.
/// @details Mirrors @ref applyCastFpToSiRteChk but applies to unsigned targets.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyCastFpToUiRteChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32 && kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

/// @brief Validate signed-integer narrowing casts.
/// @details Ensures the destination type is within the supported set before
///          recording the result.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyCastSiNarrowChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

/// @brief Validate unsigned-integer narrowing casts.
/// @details Mirrors the signed variant but applies to unsigned conversions.
/// @param ctx Verification context for the instruction.
/// @return Success or diagnostic error.
Expected<void> applyCastUiNarrowChk(const VerifyCtx &ctx, const InstructionSpec &)
{
    const auto kind = ctx.instr.type.kind;
    if (kind != Type::Kind::I16 && kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

/// @brief Force verification failure for explicitly rejected opcodes.
/// @details Emits the rejection message provided by the specification so
///          tooling can surface meaningful diagnostics for disabled opcodes.
/// @param ctx Verification context for the instruction.
/// @param spec Specification entry containing the rejection message.
/// @return Always returns a failure diagnostic.
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

/// @brief Execute the strategy-specific verifier for an instruction.
/// @details Looks up the appropriate strategy entry and invokes it.  Unknown
///          strategies fall back to the default checker so the verifier remains
///          robust even when generated data is incomplete.
/// @param ctx Verification context describing the instruction.
/// @param spec Specification entry obtained from the opcode table.
/// @return Success or diagnostic error from the strategy implementation.
Expected<void> dispatchStrategy(const VerifyCtx &ctx, const InstructionSpec &spec)
{
    const size_t index = static_cast<size_t>(spec.strategy);
    if (index >= kStrategyTable.size())
    {
        ctx.types.recordResult(ctx.instr, resolveResultType(ctx, spec));
        return checkDefault(ctx);
    }
    return kStrategyTable[index](ctx, spec);
}

/// @brief Run operand/result structural checks generated from the opcode table.
/// @details Validates operand counts, operand types, and result arity before any
///          strategy-specific verification occurs.  Failures are reported as
///          diagnostics collected from the helper checkers.
/// @param ctx Verification context describing the instruction.
/// @param spec Specification entry obtained from the opcode table.
/// @return Success or diagnostic error when checks fail.
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

/// @brief Verify an instruction using the shared context helper.
/// @details Fetches the generated specification, runs structural checks, and
///          dispatches to the appropriate verification strategy.
/// @param ctx Verification context describing the instruction.
/// @return Success or diagnostic error.
Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const InstructionSpec &spec = getInstructionSpec(ctx.instr.op);
    if (auto result = runStructuralChecks(ctx, spec); !result)
        return result;
    return dispatchStrategy(ctx, spec);
}

/// @brief Verify instruction structure without consulting external metadata.
/// @details Performs operand/result count checks for front-end generated IL
///          before the full verification context is available.  Used by
///          parsing/printing routines to confirm textual IL obeys the schema.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @return Success or diagnostic error.
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
    const bool variadicOperands = il::core::isVariadicOperandCount(spec.numOperandsMax);
    if (operandCount < spec.numOperandsMin ||
        (!variadicOperands && operandCount > spec.numOperandsMax))
    {
        std::string message;
        if (spec.numOperandsMin == spec.numOperandsMax && !variadicOperands)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(spec.numOperandsMin)) +
                      " operand";
            if (spec.numOperandsMin != 1)
                message += 's';
        }
        else if (variadicOperands)
        {
            message = "expected at least " +
                      std::to_string(static_cast<unsigned>(spec.numOperandsMin)) + " operand";
            if (spec.numOperandsMin != 1)
                message += 's';
        }
        else
        {
            message = "expected between " +
                      std::to_string(static_cast<unsigned>(spec.numOperandsMin)) + " and " +
                      std::to_string(static_cast<unsigned>(spec.numOperandsMax)) + " operands";
        }
        return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
    }

    const bool variadicSucc = il::core::isVariadicSuccessorCount(spec.numSuccessors);
    if (variadicSucc)
    {
        if (instr.labels.empty())
            return Expected<void>(makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != spec.numSuccessors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(spec.numSuccessors)) +
                                  " successor";
            if (spec.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    if (variadicSucc)
    {
        if (!instr.brArgs.empty() && instr.brArgs.size() != instr.labels.size())
        {
            return Expected<void>(makeError(
                instr.loc,
                formatInstrDiag(
                    fn, bb, instr, "expected branch argument bundle per successor or none")));
        }
    }
    else
    {
        if (instr.brArgs.size() > spec.numSuccessors)
        {
            std::string message = "expected at most " +
                                  std::to_string(static_cast<unsigned>(spec.numSuccessors)) +
                                  " branch argument bundle";
            if (spec.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != spec.numSuccessors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(spec.numSuccessors)) +
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

/// @brief Public Expected-returning wrapper for instruction verification.
/// @param ctx Verification context describing the instruction.
/// @return Success or diagnostic error.
Expected<void> verifyInstruction_E(const VerifyCtx &ctx)
{
    return verifyInstruction_impl(ctx);
}

/// @brief Wrapper that verifies structure using the data stored in @p ctx.
/// @param ctx Verification context describing the instruction.
/// @return Success or diagnostic error.
Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx)
{
    return verifyOpcodeSignature_impl(ctx.fn, ctx.block, ctx.instr);
}

/// @brief Verify an instruction using explicit context components.
/// @details Convenience overload that constructs a @ref VerifyCtx on the fly so
///          callers without a pre-built context can run the verifier.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction to verify.
/// @param externs Map of known extern declarations for call validation.
/// @param funcs Map of known functions for call validation.
/// @param types Type inference cache used to record results.
/// @param sink Diagnostic sink receiving errors and warnings.
/// @return Success or diagnostic error.
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

/// @brief Verify opcode structure using explicit function/block inputs.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @return Success or diagnostic error.
Expected<void> verifyOpcodeSignature_E(const il::core::Function &fn,
                                       const il::core::BasicBlock &bb,
                                       const il::core::Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

/// @brief Verify opcode structure and print diagnostics to a stream.
/// @details Calls the Expected-returning variant and forwards any diagnostics to
///          the provided stream for human consumption.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @param err Stream that receives diagnostic messages.
/// @return True on success; false when verification failed.
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

/// @brief Verify an instruction and print diagnostics to a stream.
/// @details Accumulates warnings in a temporary sink so both warnings and errors
///          can be emitted in order when verification fails.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @param externs Map of known extern declarations for call validation.
/// @param funcs Map of known functions for call validation.
/// @param types Type inference cache used to record results.
/// @param err Stream that receives diagnostic messages.
/// @return True on success; false otherwise.
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

/// @brief Convenience wrapper returning Expected for signature verification.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @return Success or diagnostic error.
Expected<void> verifyOpcodeSignature_expected(const il::core::Function &fn,
                                              const il::core::BasicBlock &bb,
                                              const il::core::Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

/// @brief Convenience wrapper returning Expected for instruction verification.
/// @param fn Function containing the instruction.
/// @param bb Basic block owning the instruction.
/// @param instr Instruction under verification.
/// @param externs Map of known extern declarations for call validation.
/// @param funcs Map of known functions for call validation.
/// @param types Type inference cache used to record results.
/// @param sink Diagnostic sink receiving errors and warnings.
/// @return Success or diagnostic error.
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
