//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Dispatches IL instruction verification across specialised helper modules. The
// routines in this file orchestrate operand count/type validation, result type
// inference, and opcode-specific semantic checks while threading diagnostics
// back to the caller-provided sinks.
//
//===----------------------------------------------------------------------===//

#include "il/verify/InstructionChecker.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/InstructionCheckerShared.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/VerifierTable.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace il::verify
{
namespace
{

using il::core::Opcode;
using il::support::Expected;
using il::support::makeError;
using checker::checkAddrOf;
using checker::checkAlloca;
using checker::checkBinary;
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
using checker::checkUnary;
using checker::expectAllOperandType;
using checker::fail;
using checker::kindFromClass;
using checker::typeFromClass;

/// @brief Run generic operand/result validation for the given opcode metadata.
///
/// The helper constructs dedicated checkers for operand counts, operand types,
/// and result typing.  Each checker returns an @ref Expected diagnostic so the
/// first failure encountered aborts verification with a detailed message.
///
/// @param ctx  Verification context describing the instruction under test.
/// @param info Opcode metadata retrieved from the opcode table.
/// @return Success when all checks pass; otherwise a diagnostic describing the failure.
Expected<void> checkWithInfo(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
{
    detail::OperandCountChecker countChecker(ctx, info);
    if (auto countResult = countChecker.run(); !countResult)
        return countResult;

    detail::OperandTypeChecker typeChecker(ctx, info);
    if (auto typeResult = typeChecker.run(); !typeResult)
        return typeResult;

    detail::ResultTypeChecker resultChecker(ctx, info);
    return resultChecker.run();
}

/// @brief Validate arithmetic opcodes using legacy verifier table properties.
///
/// Some arithmetic opcodes are driven by compact property tables rather than
/// full opcode metadata.  This function interprets the table entry by choosing
/// unary or binary checkers and translating operand/result classes into
/// concrete IL types.
///
/// @param ctx   Verification context describing the instruction.
/// @param props Property table entry for the opcode.
/// @return Success when the instruction satisfies the property requirements.
Expected<void> checkWithProps(const VerifyCtx &ctx, const OpProps &props)
{
    switch (props.arity)
    {
        case 1:
        {
            const auto operandKind = kindFromClass(props.operands);
            const auto resultType = typeFromClass(props.result);
            assert(operandKind && resultType);
            return checkUnary(ctx, *operandKind, *resultType);
        }
        case 2:
        {
            const auto operandKind = kindFromClass(props.operands);
            const auto resultType = typeFromClass(props.result);
            assert(operandKind && resultType);
            return checkBinary(ctx, *operandKind, *resultType);
        }
        default:
            break;
    }

    assert(false && "unsupported verifier table arity");
    return {};
}

/// @brief Perform structural validation of an instruction independent of operand semantics.
///
/// Signature checking ensures the presence or absence of a result matches the
/// opcode metadata, enforces operand and successor counts, and validates branch
/// argument bundles.  It is used both internally and by public verification
/// entry points.
///
/// @param fn     Function containing the instruction.
/// @param bb     Basic block containing the instruction.
/// @param instr  Instruction to validate.
/// @return Success when the signature is well-formed; otherwise an error diagnostic.
Expected<void> verifyOpcodeSignature_impl(const il::core::Function &fn,
                                          const il::core::BasicBlock &bb,
                                          const il::core::Instr &instr)
{
    const auto &info = il::core::getOpcodeInfo(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
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
    const bool variadic = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::string message;
        if (info.numOperandsMin == info.numOperandsMax)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " operand";
            if (info.numOperandsMin != 1)
                message += 's';
        }
        else if (variadic)
        {
            message = "expected at least " + std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " operand";
            if (info.numOperandsMin != 1)
                message += 's';
        }
        else
        {
            message = "expected between " +
                      std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " and " +
                      std::to_string(static_cast<unsigned>(info.numOperandsMax)) + " operands";
        }
        return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
    }

    const bool variadicSucc = il::core::isVariadicSuccessorCount(info.numSuccessors);
    if (variadicSucc)
    {
        if (instr.labels.empty())
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != info.numSuccessors)
        {
            std::string message = "expected " + std::to_string(static_cast<unsigned>(info.numSuccessors)) + " successor";
            if (info.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    if (variadicSucc)
    {
        if (!instr.brArgs.empty() && instr.brArgs.size() != instr.labels.size())
            return Expected<void>(makeError(instr.loc,
                                            formatInstrDiag(fn, bb, instr,
                                                            "expected branch argument bundle per successor or none")));
    }
    else
    {
        if (instr.brArgs.size() > info.numSuccessors)
        {
            std::string message = "expected at most " +
                                  std::to_string(static_cast<unsigned>(info.numSuccessors)) + " branch argument bundle";
            if (info.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != info.numSuccessors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(info.numSuccessors)) + " branch argument bundle";
            if (info.numSuccessors != 1)
                message += 's';
            message += ", or none";
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
    }

    return {};
}

/// @brief Core dispatcher that selects the appropriate verification strategy for an opcode.
///
/// The dispatcher first runs general signature validation when needed, then
/// consults either the legacy property tables or specialised opcode handlers to
/// enforce semantic rules.  Diagnostics are produced through the @ref VerifyCtx
/// to keep reporting consistent.
///
/// @param ctx Verification context describing the instruction and surrounding state.
/// @return Success when verification passes; otherwise a diagnostic failure.
Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const auto rejectUnchecked = [&](std::string_view message) {
        return fail(ctx, std::string(message));
    };

    const auto props = lookup(ctx.instr.op);
    const bool hasLegacyArithmeticProps = props && props->arity > 0 && props->arity <= 2 &&
                                          kindFromClass(props->operands).has_value() &&
                                          typeFromClass(props->result).has_value();

    if (!hasLegacyArithmeticProps)
    {
        const auto &info = il::core::getOpcodeInfo(ctx.instr.op);
        if (auto result = checkWithInfo(ctx, info); !result)
            return result;
    }

    if (hasLegacyArithmeticProps)
        return checkWithProps(ctx, *props);

    switch (ctx.instr.op)
    {
        case Opcode::Alloca:
            return checkAlloca(ctx);
        case Opcode::Add:
            return rejectUnchecked("signed integer add must use iadd.ovf (traps on overflow)");
        case Opcode::Sub:
            return rejectUnchecked("signed integer sub must use isub.ovf (traps on overflow)");
        case Opcode::Mul:
            return rejectUnchecked("signed integer mul must use imul.ovf (traps on overflow)");
        case Opcode::SDiv:
            return rejectUnchecked("signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)");
        case Opcode::UDiv:
            return rejectUnchecked("unsigned division must use udiv.chk0 (traps on divide-by-zero)");
        case Opcode::SRem:
            return rejectUnchecked("signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)");
        case Opcode::URem:
            return rejectUnchecked("unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)");
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            return checkBinary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I64));
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::SCmpLT:
        case Opcode::SCmpLE:
        case Opcode::SCmpGT:
        case Opcode::SCmpGE:
        case Opcode::UCmpLT:
        case Opcode::UCmpLE:
        case Opcode::UCmpGT:
        case Opcode::UCmpGE:
            return checkBinary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I1));
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return checkBinary(ctx, il::core::Type::Kind::F64, il::core::Type(il::core::Type::Kind::I1));
        case Opcode::Sitofp:
            return checkUnary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::F64));
        case Opcode::Fptosi:
            return rejectUnchecked("fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)");
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        {
            if (ctx.instr.type.kind != il::core::Type::Kind::I16 && ctx.instr.type.kind != il::core::Type::Kind::I32 &&
                ctx.instr.type.kind != il::core::Type::Kind::I64)
                return fail(ctx, "cast result must be i16, i32, or i64");
            return checkUnary(ctx, il::core::Type::Kind::F64, ctx.instr.type);
        }
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        {
            if (ctx.instr.type.kind != il::core::Type::Kind::I16 && ctx.instr.type.kind != il::core::Type::Kind::I32)
                return fail(ctx, "narrowing cast result must be i16 or i32");
            return checkUnary(ctx, il::core::Type::Kind::I64, ctx.instr.type);
        }
        case Opcode::IdxChk:
            return checkIdxChk(ctx);
        case Opcode::Zext1:
            return checkUnary(ctx, il::core::Type::Kind::I1, il::core::Type(il::core::Type::Kind::I64));
        case Opcode::Trunc1:
            return checkUnary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I1));
        case Opcode::GEP:
            return checkGEP(ctx);
        case Opcode::Load:
            return checkLoad(ctx);
        case Opcode::Store:
            return checkStore(ctx);
        case Opcode::AddrOf:
            return checkAddrOf(ctx);
        case Opcode::ConstStr:
            return checkConstStr(ctx);
        case Opcode::ConstNull:
            return checkConstNull(ctx);
        case Opcode::Call:
            return checkCall(ctx);
        case Opcode::TrapKind:
            return checkTrapKind(ctx);
        case Opcode::TrapFromErr:
            return checkTrapFromErr(ctx);
        case Opcode::TrapErr:
            return checkTrapErr(ctx);
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetLine:
        {
            if (auto result = expectAllOperandType(ctx, il::core::Type::Kind::Error); !result)
                return result;
            ctx.types.recordResult(ctx.instr, il::core::Type(il::core::Type::Kind::I32));
            return {};
        }
        case Opcode::ErrGetIp:
        {
            if (auto result = expectAllOperandType(ctx, il::core::Type::Kind::Error); !result)
                return result;
            ctx.types.recordResult(ctx.instr, il::core::Type(il::core::Type::Kind::I64));
            return {};
        }
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return expectAllOperandType(ctx, il::core::Type::Kind::ResumeTok);
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
            return checkDefault(ctx);
        default:
            return checkDefault(ctx);
    }
}

} // namespace

/// @brief Public Expected-returning entry point used when a full @ref VerifyCtx already exists.
///
/// Thin wrapper over @ref verifyInstruction_impl that preserves diagnostics in
/// Expected form for callers chaining verification steps.
///
/// @param ctx Verification context prepared by the caller.
/// @return Success or diagnostic failure from the underlying implementation.
Expected<void> verifyInstruction_E(const VerifyCtx &ctx)
{
    return verifyInstruction_impl(ctx);
}

/// @brief Validate instruction signature using a pre-built verification context.
///
/// Delegates to @ref verifyOpcodeSignature_impl while preserving the Expected
/// diagnostic flow.
///
/// @param ctx Verification context referencing the instruction to check.
/// @return Success when structurally valid; otherwise diagnostic failure.
Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx)
{
    return verifyOpcodeSignature_impl(ctx.fn, ctx.block, ctx.instr);
}

/// @brief Construct a verification context and run full instruction verification.
///
/// This overload accepts the raw IL entities and diagnostic infrastructure used
/// by the verifier.  It assembles a @ref VerifyCtx, delegates to the core
/// implementation, and returns the resulting Expected diagnostic.
///
/// @param fn      Function containing the instruction.
/// @param bb      Basic block containing the instruction.
/// @param instr   Instruction to verify.
/// @param externs Map of external declarations used for call validation.
/// @param funcs   Map of function declarations used for call validation.
/// @param types   Type inference cache updated with verification results.
/// @param sink    Diagnostic sink collecting warnings and errors.
/// @return Success when the instruction verifies; otherwise diagnostic failure.
Expected<void> verifyInstruction_E(const il::core::Function &fn,
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

/// @brief Verify only the structural signature of an instruction given raw IL entities.
///
/// Convenience overload that avoids constructing a full @ref VerifyCtx when
/// operand semantics are not required.
///
/// @param fn    Function containing the instruction.
/// @param bb    Basic block containing the instruction.
/// @param instr Instruction whose signature is validated.
/// @return Success when the signature is valid; otherwise diagnostic failure.
Expected<void> verifyOpcodeSignature_E(const il::core::Function &fn,
                                       const il::core::BasicBlock &bb,
                                       const il::core::Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

/// @brief Convenience wrapper that prints diagnostics to a stream when signature verification fails.
///
/// Calls the Expected-based API and, upon failure, forwards diagnostics to the
/// provided stream.  Returns a boolean suitable for lightweight callers that do
/// not need structured diagnostics.
///
/// @param fn    Function containing the instruction.
/// @param bb    Basic block containing the instruction.
/// @param instr Instruction to verify.
/// @param err   Stream receiving diagnostic text when verification fails.
/// @return True on success; false when verification reports an error.
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

/// @brief Convenience wrapper that prints diagnostics while performing full instruction verification.
///
/// Uses a @ref CollectingDiagSink to gather warnings so they can be emitted to
/// the caller-provided stream even when verification succeeds.
///
/// @param fn      Function containing the instruction.
/// @param bb      Basic block containing the instruction.
/// @param instr   Instruction to verify.
/// @param externs Map of external declarations for call validation.
/// @param funcs   Map of function declarations for call validation.
/// @param types   Type inference cache updated with verification results.
/// @param err     Stream receiving diagnostic output.
/// @return True when verification succeeds; false otherwise.
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

/// @brief Re-export of signature verification that preserves the Expected interface for callers.
///
/// Mirrors @ref verifyOpcodeSignature_E so downstream code can use a naming
/// convention that distinguishes Expected-returning functions.
///
/// @param fn    Function containing the instruction.
/// @param bb    Basic block containing the instruction.
/// @param instr Instruction to validate.
/// @return Success when the signature verifies; otherwise diagnostic failure.
Expected<void> verifyOpcodeSignature_expected(const il::core::Function &fn,
                                               const il::core::BasicBlock &bb,
                                               const il::core::Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

/// @brief Re-export of full instruction verification maintaining Expected semantics.
///
/// Provided for symmetry with other verification entry points so callers can
/// depend on consistent naming.
///
/// @param fn      Function containing the instruction.
/// @param bb      Basic block containing the instruction.
/// @param instr   Instruction to verify.
/// @param externs Map of external declarations for call validation.
/// @param funcs   Map of function declarations for call validation.
/// @param types   Type inference cache updated with verification results.
/// @param sink    Diagnostic sink collecting warnings and errors.
/// @return Success when verification passes; otherwise diagnostic failure.
Expected<void> verifyInstruction_expected(const il::core::Function &fn,
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
