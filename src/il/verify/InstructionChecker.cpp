// File: src/il/verify/InstructionChecker.cpp
// Purpose: Dispatches instruction verification across specialized helper modules.
// Key invariants: Each opcode is checked exactly once with consistent operand and result typing rules.
// Ownership/Lifetime: Operates on caller-provided verification context without owning IL objects.
// Links: docs/il-guide.md#reference

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

Expected<void> verifyInstruction_E(const VerifyCtx &ctx)
{
    return verifyInstruction_impl(ctx);
}

Expected<void> verifyOpcodeSignature_E(const VerifyCtx &ctx)
{
    return verifyOpcodeSignature_impl(ctx.fn, ctx.block, ctx.instr);
}

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
