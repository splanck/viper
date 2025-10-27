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

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/Diagnostics.hpp"
#include "il/verify/InstructionCheckUtils.hpp"
#include "il/verify/InstructionCheckerShared.hpp"
#include "il/verify/Rules.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/VerifierTable.hpp"
#include "support/diag_expected.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace il::verify
{
namespace
{

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
using il::core::Opcode;
using il::core::Type;
using il::support::Expected;
using il::support::makeError;

Expected<void> applyRegisteredRules(const VerifyCtx &ctx)
{
    const auto &rules = viper_verifier_rules();

    int blockIndex = -1;
    for (size_t idx = 0; idx < ctx.fn.blocks.size(); ++idx)
    {
        if (&ctx.fn.blocks[idx] == &ctx.block)
        {
            blockIndex = static_cast<int>(idx);
            break;
        }
    }

    int instrIndex = -1;
    for (size_t idx = 0; idx < ctx.block.instructions.size(); ++idx)
    {
        if (&ctx.block.instructions[idx] == &ctx.instr)
        {
            instrIndex = static_cast<int>(idx);
            break;
        }
    }

    for (const Rule &rule : rules)
    {
        if (std::string_view(rule.name).rfind("eh.", 0) == 0)
            continue;

        std::string message;
        if (rule.check(ctx.fn, ctx.instr, message))
            continue;

        const std::string diagMessage =
            diag_rule_msg(rule.name, message, ctx.fn.name, blockIndex, instrIndex);
        return Expected<void>(makeError(ctx.instr.loc, diagMessage));
    }

    return {};
}

using Instruction = il::core::Instr;

template <typename T> using ErrorOr = Expected<T>;

using VerifierFn = ErrorOr<void> (*)(const Instruction &, const VerifyCtx &);

ErrorOr<void> rejectUnchecked(const VerifyCtx &ctx, std::string_view message)
{
    return fail(ctx, std::string(message));
}

ErrorOr<void> checkBinaryI64ToI64(const VerifyCtx &ctx)
{
    return checkBinary(ctx, Type::Kind::I64, Type(Type::Kind::I64));
}

ErrorOr<void> checkBinaryI64ToI1(const VerifyCtx &ctx)
{
    return checkBinary(ctx, Type::Kind::I64, Type(Type::Kind::I1));
}

ErrorOr<void> checkBinaryF64ToI1(const VerifyCtx &ctx)
{
    return checkBinary(ctx, Type::Kind::F64, Type(Type::Kind::I1));
}

ErrorOr<void> checkUnaryI64ToF64(const VerifyCtx &ctx)
{
    return checkUnary(ctx, Type::Kind::I64, Type(Type::Kind::F64));
}

ErrorOr<void> checkUnaryF64ToResult(const VerifyCtx &ctx)
{
    return checkUnary(ctx, Type::Kind::F64, ctx.instr.type);
}

ErrorOr<void> checkUnaryI64ToResult(const VerifyCtx &ctx)
{
    return checkUnary(ctx, Type::Kind::I64, ctx.instr.type);
}

ErrorOr<void> verifyAlloca(const Instruction &, const VerifyCtx &ctx)
{
    return checkAlloca(ctx);
}

ErrorOr<void> verifyAdd(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx, "signed integer add must use iadd.ovf (traps on overflow)");
}

ErrorOr<void> verifySub(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx, "signed integer sub must use isub.ovf (traps on overflow)");
}

ErrorOr<void> verifyMul(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx, "signed integer mul must use imul.ovf (traps on overflow)");
}

ErrorOr<void> verifySDiv(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(
        ctx, "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)");
}

ErrorOr<void> verifyUDiv(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx, "unsigned division must use udiv.chk0 (traps on divide-by-zero)");
}

ErrorOr<void> verifySRem(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx,
                           "signed remainder must use srem.chk0 (traps on divide-by-zero; matches "
                           "BASIC MOD semantics)");
}

ErrorOr<void> verifyURem(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx,
                           "unsigned remainder must use urem.chk0 (traps on divide-by-zero; "
                           "matches BASIC MOD semantics)");
}

ErrorOr<void> verifyUDivChk0(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifySRemChk0(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyURemChk0(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyAnd(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyOr(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyXor(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyShl(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyLShr(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyAShr(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI64(ctx);
}

ErrorOr<void> verifyICmpEq(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyICmpNe(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifySCmpLT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifySCmpLE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifySCmpGT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifySCmpGE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyUCmpLT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyUCmpLE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyUCmpGT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyUCmpGE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryI64ToI1(ctx);
}

ErrorOr<void> verifyFCmpEQ(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifyFCmpNE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifyFCmpLT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifyFCmpLE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifyFCmpGT(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifyFCmpGE(const Instruction &, const VerifyCtx &ctx)
{
    return checkBinaryF64ToI1(ctx);
}

ErrorOr<void> verifySitofp(const Instruction &, const VerifyCtx &ctx)
{
    return checkUnaryI64ToF64(ctx);
}

ErrorOr<void> verifyFptosi(const Instruction &, const VerifyCtx &ctx)
{
    return rejectUnchecked(ctx,
                           "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to "
                           "nearest-even and traps on "
                           "overflow)");
}

ErrorOr<void> verifyCastFpToSiRteChk(const Instruction &, const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checkUnaryF64ToResult(ctx);
}

ErrorOr<void> verifyCastFpToUiRteChk(const Instruction &, const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
        ctx.instr.type.kind != Type::Kind::I64)
        return fail(ctx, "cast result must be i16, i32, or i64");
    return checkUnaryF64ToResult(ctx);
}

ErrorOr<void> verifyCastSiNarrowChk(const Instruction &, const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checkUnaryI64ToResult(ctx);
}

ErrorOr<void> verifyCastUiNarrowChk(const Instruction &, const VerifyCtx &ctx)
{
    if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "narrowing cast result must be i16 or i32");
    return checkUnaryI64ToResult(ctx);
}

ErrorOr<void> verifyIdxChk(const Instruction &, const VerifyCtx &ctx)
{
    return checkIdxChk(ctx);
}

ErrorOr<void> verifyZext1(const Instruction &, const VerifyCtx &ctx)
{
    return checkUnary(ctx, Type::Kind::I1, Type(Type::Kind::I64));
}

ErrorOr<void> verifyTrunc1(const Instruction &, const VerifyCtx &ctx)
{
    return checkUnary(ctx, Type::Kind::I64, Type(Type::Kind::I1));
}

ErrorOr<void> verifyGEP(const Instruction &, const VerifyCtx &ctx)
{
    return checkGEP(ctx);
}

ErrorOr<void> verifyLoad(const Instruction &, const VerifyCtx &ctx)
{
    return checkLoad(ctx);
}

ErrorOr<void> verifyStore(const Instruction &, const VerifyCtx &ctx)
{
    return checkStore(ctx);
}

ErrorOr<void> verifyAddrOf(const Instruction &, const VerifyCtx &ctx)
{
    return checkAddrOf(ctx);
}

ErrorOr<void> verifyConstStr(const Instruction &, const VerifyCtx &ctx)
{
    return checkConstStr(ctx);
}

ErrorOr<void> verifyConstNull(const Instruction &, const VerifyCtx &ctx)
{
    return checkConstNull(ctx);
}

ErrorOr<void> verifyCall(const Instruction &, const VerifyCtx &ctx)
{
    return checkCall(ctx);
}

ErrorOr<void> verifyTrapKind(const Instruction &, const VerifyCtx &ctx)
{
    return checkTrapKind(ctx);
}

ErrorOr<void> verifyTrapFromErr(const Instruction &, const VerifyCtx &ctx)
{
    return checkTrapFromErr(ctx);
}

ErrorOr<void> verifyTrapErr(const Instruction &, const VerifyCtx &ctx)
{
    return checkTrapErr(ctx);
}

ErrorOr<void> verifyErrGetKind(const Instruction &, const VerifyCtx &ctx)
{
    if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
        return result;
    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I32));
    return {};
}

ErrorOr<void> verifyErrGetCode(const Instruction &, const VerifyCtx &ctx)
{
    if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
        return result;
    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I32));
    return {};
}

ErrorOr<void> verifyErrGetLine(const Instruction &, const VerifyCtx &ctx)
{
    if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
        return result;
    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I32));
    return {};
}

ErrorOr<void> verifyErrGetIp(const Instruction &, const VerifyCtx &ctx)
{
    if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
        return result;
    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
    return {};
}

ErrorOr<void> verifyResumeSame(const Instruction &, const VerifyCtx &ctx)
{
    return expectAllOperandType(ctx, Type::Kind::ResumeTok);
}

ErrorOr<void> verifyResumeNext(const Instruction &, const VerifyCtx &ctx)
{
    return expectAllOperandType(ctx, Type::Kind::ResumeTok);
}

ErrorOr<void> verifyResumeLabel(const Instruction &, const VerifyCtx &ctx)
{
    return expectAllOperandType(ctx, Type::Kind::ResumeTok);
}

static const std::pair<Opcode, VerifierFn> kVerifiers[] = {
    {Opcode::Add, &verifyAdd},
    {Opcode::Sub, &verifySub},
    {Opcode::Mul, &verifyMul},
    {Opcode::SDiv, &verifySDiv},
    {Opcode::UDiv, &verifyUDiv},
    {Opcode::SRem, &verifySRem},
    {Opcode::URem, &verifyURem},
    {Opcode::UDivChk0, &verifyUDivChk0},
    {Opcode::SRemChk0, &verifySRemChk0},
    {Opcode::URemChk0, &verifyURemChk0},
    {Opcode::IdxChk, &verifyIdxChk},
    {Opcode::And, &verifyAnd},
    {Opcode::Or, &verifyOr},
    {Opcode::Xor, &verifyXor},
    {Opcode::Shl, &verifyShl},
    {Opcode::LShr, &verifyLShr},
    {Opcode::AShr, &verifyAShr},
    {Opcode::ICmpEq, &verifyICmpEq},
    {Opcode::ICmpNe, &verifyICmpNe},
    {Opcode::SCmpLT, &verifySCmpLT},
    {Opcode::SCmpLE, &verifySCmpLE},
    {Opcode::SCmpGT, &verifySCmpGT},
    {Opcode::SCmpGE, &verifySCmpGE},
    {Opcode::UCmpLT, &verifyUCmpLT},
    {Opcode::UCmpLE, &verifyUCmpLE},
    {Opcode::UCmpGT, &verifyUCmpGT},
    {Opcode::UCmpGE, &verifyUCmpGE},
    {Opcode::FCmpEQ, &verifyFCmpEQ},
    {Opcode::FCmpNE, &verifyFCmpNE},
    {Opcode::FCmpLT, &verifyFCmpLT},
    {Opcode::FCmpLE, &verifyFCmpLE},
    {Opcode::FCmpGT, &verifyFCmpGT},
    {Opcode::FCmpGE, &verifyFCmpGE},
    {Opcode::Sitofp, &verifySitofp},
    {Opcode::Fptosi, &verifyFptosi},
    {Opcode::CastFpToSiRteChk, &verifyCastFpToSiRteChk},
    {Opcode::CastFpToUiRteChk, &verifyCastFpToUiRteChk},
    {Opcode::CastSiNarrowChk, &verifyCastSiNarrowChk},
    {Opcode::CastUiNarrowChk, &verifyCastUiNarrowChk},
    {Opcode::Zext1, &verifyZext1},
    {Opcode::Trunc1, &verifyTrunc1},
    {Opcode::Alloca, &verifyAlloca},
    {Opcode::GEP, &verifyGEP},
    {Opcode::Load, &verifyLoad},
    {Opcode::Store, &verifyStore},
    {Opcode::AddrOf, &verifyAddrOf},
    {Opcode::ConstStr, &verifyConstStr},
    {Opcode::ConstNull, &verifyConstNull},
    {Opcode::Call, &verifyCall},
    {Opcode::TrapKind, &verifyTrapKind},
    {Opcode::TrapFromErr, &verifyTrapFromErr},
    {Opcode::TrapErr, &verifyTrapErr},
    {Opcode::ErrGetKind, &verifyErrGetKind},
    {Opcode::ErrGetCode, &verifyErrGetCode},
    {Opcode::ErrGetIp, &verifyErrGetIp},
    {Opcode::ErrGetLine, &verifyErrGetLine},
    {Opcode::ResumeSame, &verifyResumeSame},
    {Opcode::ResumeNext, &verifyResumeNext},
    {Opcode::ResumeLabel, &verifyResumeLabel},
};

VerifierFn lookupVerifier(Opcode opcode)
{
    const auto begin = std::begin(kVerifiers);
    const auto end = std::end(kVerifiers);
    const auto it =
        std::lower_bound(begin,
                         end,
                         opcode,
                         [](const auto &entry, Opcode value)
                         {
                             return static_cast<std::underlying_type_t<Opcode>>(entry.first) <
                                    static_cast<std::underlying_type_t<Opcode>>(value);
                         });
    if (it != end && it->first == opcode)
        return it->second;
    return nullptr;
}

bool hasLegacyArithmeticProps(const std::optional<OpProps> &props)
{
    if (!props)
        return false;
    if (props->arity == 0 || props->arity > 2)
        return false;
    return kindFromClass(props->operands).has_value() && typeFromClass(props->result).has_value();
}

/// @brief Validate operands and results using canonical opcode metadata.
/// @details Runs the operand-count, operand-type, and result-type checkers in
///          sequence using @ref il::core::OpcodeInfo to describe the opcode.
///          Any failure propagates immediately through the @ref Expected
///          channel.
/// @param ctx Verification context describing the instruction under test.
/// @param info Opcode metadata record retrieved from the core tables.
/// @return Empty success or a diagnostic describing the mismatch.
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

/// @brief Validate arithmetic-style opcodes using verifier table properties.
/// @details Converts the compact verifier table representation into operand and
///          result kinds understood by the shared arithmetic checkers.  Supports
///          unary and binary shapes and asserts for unexpected table entries.
/// @param ctx Verification context describing the instruction under test.
/// @param props Verifier table metadata for the opcode.
/// @return Empty success or a diagnostic failure.
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
    const auto &info = il::core::getOpcodeInfo(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
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
    const bool variadic = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::string message;
        if (info.numOperandsMin == info.numOperandsMax)
        {
            message = "expected " + std::to_string(static_cast<unsigned>(info.numOperandsMin)) +
                      " operand";
            if (info.numOperandsMin != 1)
                message += 's';
        }
        else if (variadic)
        {
            message = "expected at least " +
                      std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " operand";
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
            return Expected<void>(makeError(
                instr.loc, formatInstrDiag(fn, bb, instr, "expected at least 1 successor")));
    }
    else
    {
        if (instr.labels.size() != info.numSuccessors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(info.numSuccessors)) +
                                  " successor";
            if (info.numSuccessors != 1)
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
        if (instr.brArgs.size() > info.numSuccessors)
        {
            std::string message = "expected at most " +
                                  std::to_string(static_cast<unsigned>(info.numSuccessors)) +
                                  " branch argument bundle";
            if (info.numSuccessors != 1)
                message += 's';
            return Expected<void>(makeError(instr.loc, formatInstrDiag(fn, bb, instr, message)));
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != info.numSuccessors)
        {
            std::string message = "expected " +
                                  std::to_string(static_cast<unsigned>(info.numSuccessors)) +
                                  " branch argument bundle";
            if (info.numSuccessors != 1)
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
    if (auto ruleResult = applyRegisteredRules(ctx); !ruleResult)
        return ruleResult;

    const auto props = lookup(ctx.instr.op);
    if (hasLegacyArithmeticProps(props))
        return checkWithProps(ctx, *props);

    const auto &info = il::core::getOpcodeInfo(ctx.instr.op);
    if (auto result = checkWithInfo(ctx, info); !result)
        return result;

    if (const auto verifier = lookupVerifier(ctx.instr.op))
        return verifier(ctx.instr, ctx);

    return checkDefault(ctx);
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
