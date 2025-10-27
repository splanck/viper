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
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/InstructionCheckerShared.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/SpecTables.hpp"
#include "support/diag_expected.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
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
using il::core::Opcode;
using il::core::Type;
using il::support::Expected;
using il::support::makeError;

Expected<void> rejectUnchecked(const VerifyCtx &ctx, std::string_view message)
{
    return fail(ctx, std::string(message));
}

Expected<void> recordResultFromSpec(const VerifyCtx &ctx, const spec::InstructionSpec &spec)
{
    if (!ctx.instr.result)
        return {};

    if (spec.result.type == il::core::TypeCategory::InstrType)
    {
        ctx.types.recordResult(ctx.instr, ctx.instr.type);
        return {};
    }

    if (auto kind = detail::kindFromCategory(spec.result.type))
        ctx.types.recordResult(ctx.instr, Type(*kind));

    return {};
}

Type resolveResultTypeFromSpec(const VerifyCtx &ctx, const spec::InstructionSpec &spec)
{
    if (spec.result.type == il::core::TypeCategory::InstrType)
        return ctx.instr.type;

    if (auto kind = detail::kindFromCategory(spec.result.type))
        return Type(*kind);

    return ctx.instr.type;
}

Expected<void> applyBinaryFromSpec(const VerifyCtx &ctx, const spec::InstructionSpec &spec)
{
    const auto lhsKind = detail::kindFromCategory(spec.operands.types[0]);
    const auto rhsKind = detail::kindFromCategory(spec.operands.types[1]);
    assert(lhsKind && rhsKind && *lhsKind == *rhsKind && "binary spec must provide matching operand kinds");
    if (!lhsKind || !rhsKind || *lhsKind != *rhsKind)
        return checker::checkDefault(ctx);

    return checker::checkBinary(ctx, *lhsKind, resolveResultTypeFromSpec(ctx, spec));
}

Expected<void> applyUnaryFromSpec(const VerifyCtx &ctx, const spec::InstructionSpec &spec)
{
    const auto operandKind = detail::kindFromCategory(spec.operands.types[0]);
    assert(operandKind && "unary spec must provide operand kind");
    if (!operandKind)
        return checker::checkDefault(ctx);

    return checker::checkUnary(ctx, *operandKind, resolveResultTypeFromSpec(ctx, spec));
}

Expected<void> applyCastFpToSiRteChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checker::checkUnary(ctx, Type::Kind::F64, ctx.instr.type);
}

Expected<void> applyCastFpToUiRteChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checker::checkUnary(ctx, Type::Kind::F64, ctx.instr.type);
}

Expected<void> applyCastSiNarrowChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checker::checkUnary(ctx, Type::Kind::I64, ctx.instr.type);
}

Expected<void> applyCastUiNarrowChk(const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checker::checkUnary(ctx, Type::Kind::I64, ctx.instr.type);
}

Expected<void> applyIdxChk(const VerifyCtx &ctx)
{
    return checkIdxChk(ctx);
}

Expected<void> applyAlloca(const VerifyCtx &ctx)
{
    return checkAlloca(ctx);
}

Expected<void> applyGEP(const VerifyCtx &ctx)
{
    return checkGEP(ctx);
}

Expected<void> applyLoad(const VerifyCtx &ctx)
{
    return checkLoad(ctx);
}

Expected<void> applyStore(const VerifyCtx &ctx)
{
    return checkStore(ctx);
}

Expected<void> applyAddrOf(const VerifyCtx &ctx)
{
    return checkAddrOf(ctx);
}

Expected<void> applyConstStr(const VerifyCtx &ctx)
{
    return checkConstStr(ctx);
}

Expected<void> applyConstNull(const VerifyCtx &ctx)
{
    return checkConstNull(ctx);
}

Expected<void> applyCall(const VerifyCtx &ctx)
{
    return checkCall(ctx);
}

Expected<void> applyTrapKind(const VerifyCtx &ctx)
{
    return checkTrapKind(ctx);
}

Expected<void> applyTrapFromErr(const VerifyCtx &ctx)
{
    return checkTrapFromErr(ctx);
}

Expected<void> applyTrapErr(const VerifyCtx &ctx)
{
    return checkTrapErr(ctx);
}

enum class SemanticKind
{
    RecordResult,
    Reject,
    Alloca,
    GEP,
    Load,
    Store,
    AddrOf,
    ConstStr,
    ConstNull,
    Call,
    TrapKind,
    TrapFromErr,
    TrapErr,
    IdxChk,
    CastFpToSiRteChk,
    CastFpToUiRteChk,
    CastSiNarrowChk,
    CastUiNarrowChk,
    BinaryFromSpec,
    UnaryFromSpec,
};

enum class StructuralPhase
{
    None,
    Before,
    After,
};

struct SemanticRule
{
    SemanticKind kind = SemanticKind::RecordResult;
    const char *message = nullptr;
    StructuralPhase structural = StructuralPhase::Before;
};

constexpr std::array<SemanticRule, il::core::kNumOpcodes> buildSemanticTable()
{
    std::array<SemanticRule, il::core::kNumOpcodes> table{};
    for (auto &entry : table)
        entry = {SemanticKind::RecordResult, nullptr, StructuralPhase::Before};

    table[static_cast<std::size_t>(Opcode::Add)] =
        {SemanticKind::Reject,
         "signed integer add must use iadd.ovf (traps on overflow)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::Sub)] =
        {SemanticKind::Reject,
         "signed integer sub must use isub.ovf (traps on overflow)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::Mul)] =
        {SemanticKind::Reject,
         "signed integer mul must use imul.ovf (traps on overflow)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::SDiv)] =
        {SemanticKind::Reject,
         "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::UDiv)] =
        {SemanticKind::Reject,
         "unsigned division must use udiv.chk0 (traps on divide-by-zero)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::SRem)] =
        {SemanticKind::Reject,
         "signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::URem)] =
        {SemanticKind::Reject,
         "unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)",
         StructuralPhase::None};
    table[static_cast<std::size_t>(Opcode::Fptosi)] =
        {SemanticKind::Reject,
         "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)",
         StructuralPhase::None};

    const auto setBinary = [&](Opcode op) {
        table[static_cast<std::size_t>(op)] =
            {SemanticKind::BinaryFromSpec, nullptr, StructuralPhase::After};
    };
    setBinary(Opcode::IAddOvf);
    setBinary(Opcode::ISubOvf);
    setBinary(Opcode::IMulOvf);
    setBinary(Opcode::SDivChk0);
    setBinary(Opcode::UDivChk0);
    setBinary(Opcode::SRemChk0);
    setBinary(Opcode::URemChk0);
    setBinary(Opcode::And);
    setBinary(Opcode::Or);
    setBinary(Opcode::Xor);
    setBinary(Opcode::Shl);
    setBinary(Opcode::LShr);
    setBinary(Opcode::AShr);
    setBinary(Opcode::FAdd);
    setBinary(Opcode::FSub);
    setBinary(Opcode::FMul);
    setBinary(Opcode::FDiv);
    setBinary(Opcode::ICmpEq);
    setBinary(Opcode::ICmpNe);
    setBinary(Opcode::SCmpLT);
    setBinary(Opcode::SCmpLE);
    setBinary(Opcode::SCmpGT);
    setBinary(Opcode::SCmpGE);
    setBinary(Opcode::UCmpLT);
    setBinary(Opcode::UCmpLE);
    setBinary(Opcode::UCmpGT);
    setBinary(Opcode::UCmpGE);
    setBinary(Opcode::FCmpEQ);
    setBinary(Opcode::FCmpNE);
    setBinary(Opcode::FCmpLT);
    setBinary(Opcode::FCmpLE);
    setBinary(Opcode::FCmpGT);
    setBinary(Opcode::FCmpGE);

    const auto setUnary = [&](Opcode op) {
        table[static_cast<std::size_t>(op)] =
            {SemanticKind::UnaryFromSpec, nullptr, StructuralPhase::After};
    };
    setUnary(Opcode::CastSiToFp);
    setUnary(Opcode::CastUiToFp);
    setUnary(Opcode::Zext1);
    setUnary(Opcode::Trunc1);
    setUnary(Opcode::ErrGetKind);
    setUnary(Opcode::ErrGetCode);
    setUnary(Opcode::ErrGetIp);
    setUnary(Opcode::ErrGetLine);

    table[static_cast<std::size_t>(Opcode::Alloca)] =
        {SemanticKind::Alloca, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::GEP)] =
        {SemanticKind::GEP, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::Load)] =
        {SemanticKind::Load, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::Store)] =
        {SemanticKind::Store, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::AddrOf)] =
        {SemanticKind::AddrOf, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::ConstStr)] =
        {SemanticKind::ConstStr, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::ConstNull)] =
        {SemanticKind::ConstNull, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::Call)] =
        {SemanticKind::Call, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::TrapKind)] =
        {SemanticKind::TrapKind, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::TrapFromErr)] =
        {SemanticKind::TrapFromErr, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::TrapErr)] =
        {SemanticKind::TrapErr, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::IdxChk)] =
        {SemanticKind::IdxChk, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::CastFpToSiRteChk)] =
        {SemanticKind::CastFpToSiRteChk, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::CastFpToUiRteChk)] =
        {SemanticKind::CastFpToUiRteChk, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::CastSiNarrowChk)] =
        {SemanticKind::CastSiNarrowChk, nullptr, StructuralPhase::After};
    table[static_cast<std::size_t>(Opcode::CastUiNarrowChk)] =
        {SemanticKind::CastUiNarrowChk, nullptr, StructuralPhase::After};

    return table;
}

const auto kSemanticTable = buildSemanticTable();

Expected<void> applySemanticRule(const VerifyCtx &ctx,
                                 const spec::InstructionSpec &spec,
                                 const SemanticRule &rule)
{
    switch (rule.kind)
    {
        case SemanticKind::RecordResult:
            return recordResultFromSpec(ctx, spec);
        case SemanticKind::Reject:
            return rejectUnchecked(ctx, rule.message);
        case SemanticKind::Alloca:
            return applyAlloca(ctx);
        case SemanticKind::GEP:
            return applyGEP(ctx);
        case SemanticKind::Load:
            return applyLoad(ctx);
        case SemanticKind::Store:
            return applyStore(ctx);
        case SemanticKind::AddrOf:
            return applyAddrOf(ctx);
        case SemanticKind::ConstStr:
            return applyConstStr(ctx);
        case SemanticKind::ConstNull:
            return applyConstNull(ctx);
        case SemanticKind::Call:
            return applyCall(ctx);
        case SemanticKind::TrapKind:
            return applyTrapKind(ctx);
        case SemanticKind::TrapFromErr:
            return applyTrapFromErr(ctx);
        case SemanticKind::TrapErr:
            return applyTrapErr(ctx);
        case SemanticKind::IdxChk:
            return applyIdxChk(ctx);
        case SemanticKind::CastFpToSiRteChk:
            return applyCastFpToSiRteChk(ctx);
        case SemanticKind::CastFpToUiRteChk:
            return applyCastFpToUiRteChk(ctx);
        case SemanticKind::CastSiNarrowChk:
            return applyCastSiNarrowChk(ctx);
        case SemanticKind::CastUiNarrowChk:
            return applyCastUiNarrowChk(ctx);
        case SemanticKind::BinaryFromSpec:
            return applyBinaryFromSpec(ctx, spec);
        case SemanticKind::UnaryFromSpec:
            return applyUnaryFromSpec(ctx, spec);
    }

    return {};
}

Expected<void> checkWithSpec(const VerifyCtx &ctx, const spec::InstructionSpec &spec)
{
    detail::OperandCountChecker countChecker(ctx, spec);
    if (auto countResult = countChecker.run(); !countResult)
        return countResult;

    detail::OperandTypeChecker typeChecker(ctx, spec);
    if (auto typeResult = typeChecker.run(); !typeResult)
        return typeResult;

    detail::ResultTypeChecker resultChecker(ctx, spec);
    return resultChecker.run();
}

/// @brief Check an instruction's structural signature against metadata.
/// @details Validates result presence, operand counts, successor arity, and
///          branch argument bundles using @ref il::core::OpcodeInfo.  Produces
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
    const auto &spec = spec::lookup(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (spec.result.arity)
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
    const bool variadic = il::core::isVariadicOperandCount(spec.operands.max);
    if (operandCount < spec.operands.min || (!variadic && operandCount > spec.operands.max))
    {
        std::string message;
        if (spec.operands.min == spec.operands.max)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(spec.operands.min)) +
                      " operand";
            if (spec.operands.min != 1)
                message += 's';
        }
        else if (variadic)
        {
            message = "expected at least " +
                      std::to_string(static_cast<unsigned>(spec.operands.min)) + " operand";
            if (spec.operands.min != 1)
                message += 's';
        }
        else
        {
            message = "expected between " +
                      std::to_string(static_cast<unsigned>(spec.operands.min)) + " and " +
                      std::to_string(static_cast<unsigned>(spec.operands.max)) + " operands";
        }
        return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
    }

    const bool variadicSucc = il::core::isVariadicSuccessorCount(spec.flags.successors);
    if (variadicSucc)
    {
        if (instr.labels.empty())
            return Expected<void>(makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != spec.flags.successors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(spec.flags.successors)) +
                                  " successor";
            if (spec.flags.successors != 1)
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
        if (instr.brArgs.size() > spec.flags.successors)
        {
            std::string message = "expected at most " +
                                  std::to_string(static_cast<unsigned>(spec.flags.successors)) +
                                  " branch argument bundle";
            if (spec.flags.successors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != spec.flags.successors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(spec.flags.successors)) +
                                  " branch argument bundle";
            if (spec.flags.successors != 1)
                message += 's';
            message += ", or none";
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    return {};
}

/// @brief Perform full semantic verification for a single instruction.
/// @details Combines metadata-driven checks with opcode-specific handlers to
///          ensure operands, results, and side effects obey the IL specification
///          and runtime conventions.
/// @param ctx Verification context that supplies type inference, extern data,
///        and diagnostic sinks.
/// @return Empty success or a diagnostic when verification fails.
Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const auto &specEntry = spec::lookup(ctx.instr.op);
    const auto &rule = kSemanticTable[static_cast<std::size_t>(ctx.instr.op)];
    if (rule.structural == StructuralPhase::Before)
    {
        if (auto result = checkWithSpec(ctx, specEntry); !result)
            return result;
    }

    if (auto result = applySemanticRule(ctx, specEntry, rule); !result)
        return result;

    if (rule.structural == StructuralPhase::After)
    {
        if (auto result = checkWithSpec(ctx, specEntry); !result)
            return result;
    }

    return {};
}

} // namespace

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
