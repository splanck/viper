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
#include "il/verify/TypeInference.hpp"
#include "il/verify/OperandCountChecker.hpp"
#include "il/verify/OperandTypeChecker.hpp"
#include "il/verify/ResultTypeChecker.hpp"
#include "il/verify/VerifierTable.hpp"
#include "support/diag_expected.hpp"

#include <array>
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

using Status = Expected<void>;

struct Rule
{
    virtual ~Rule() = default;

    virtual bool applies(const il::core::Instr &instr) const = 0;
    virtual Status check(const VerifyCtx &ctx) const = 0;
};

Expected<void> checkWithProps(const VerifyCtx &ctx, const OpProps &props);

std::optional<OpProps> lookupLegacyArithmeticProps(const il::core::Instr &instr)
{
    const auto props = lookup(instr.op);
    if (!props)
        return std::nullopt;

    const bool supportedArity = props->arity > 0 && props->arity <= 2;
    const bool hasOperandKind = kindFromClass(props->operands).has_value();
    const bool hasResultType = typeFromClass(props->result).has_value();
    if (!supportedArity || !hasOperandKind || !hasResultType)
        return std::nullopt;

    return props;
}

class LegacyArithmeticRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return lookupLegacyArithmeticProps(instr).has_value();
    }

    Status check(const VerifyCtx &ctx) const override
    {
        const auto props = lookupLegacyArithmeticProps(ctx.instr);
        assert(props.has_value());
        return checkWithProps(ctx, *props);
    }
};

class ForbiddenArithmeticRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (const auto &entry : kEntries)
        {
            if (instr.op == entry.opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        for (const auto &entry : kEntries)
        {
            if (ctx.instr.op == entry.opcode)
                return fail(ctx, std::string(entry.message));
        }
        return fail(ctx, "unsupported opcode");
    }

  private:
    struct Entry
    {
        Opcode opcode;
        std::string_view message;
    };

    static constexpr std::array<Entry, 8> kEntries{{
        {Opcode::Add, "signed integer add must use iadd.ovf (traps on overflow)"},
        {Opcode::Sub, "signed integer sub must use isub.ovf (traps on overflow)"},
        {Opcode::Mul, "signed integer mul must use imul.ovf (traps on overflow)"},
        {Opcode::SDiv, "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)"},
        {Opcode::UDiv, "unsigned division must use udiv.chk0 (traps on divide-by-zero)"},
        {Opcode::SRem, "signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)"},
        {Opcode::URem, "unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)"},
        {Opcode::Fptosi, "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)"},
    }};
};

class BinaryI64Rule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkBinary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I64));
    }

  private:
    static constexpr std::array<Opcode, 9> kOpcodes{
        Opcode::UDivChk0, Opcode::SRemChk0, Opcode::URemChk0, Opcode::And,
        Opcode::Or,        Opcode::Xor,      Opcode::Shl,      Opcode::LShr,
        Opcode::AShr,
    };
};

class IntegerCompareRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkBinary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I1));
    }

  private:
    static constexpr std::array<Opcode, 10> kOpcodes{
        Opcode::ICmpEq, Opcode::ICmpNe, Opcode::SCmpLT, Opcode::SCmpLE, Opcode::SCmpGT,
        Opcode::SCmpGE, Opcode::UCmpLT, Opcode::UCmpLE, Opcode::UCmpGT, Opcode::UCmpGE,
    };
};

class FloatCompareRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkBinary(ctx, il::core::Type::Kind::F64, il::core::Type(il::core::Type::Kind::I1));
    }

  private:
    static constexpr std::array<Opcode, 6> kOpcodes{
        Opcode::FCmpEQ, Opcode::FCmpNE, Opcode::FCmpLT,
        Opcode::FCmpLE, Opcode::FCmpGT, Opcode::FCmpGE,
    };
};

class SiToFpRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::Sitofp;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkUnary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::F64));
    }
};

class CastFpToIntRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::CastFpToSiRteChk || instr.op == Opcode::CastFpToUiRteChk;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        const auto kind = ctx.instr.type.kind;
        if (kind != il::core::Type::Kind::I16 && kind != il::core::Type::Kind::I32 && kind != il::core::Type::Kind::I64)
            return fail(ctx, "cast result must be i16, i32, or i64");
        return checkUnary(ctx, il::core::Type::Kind::F64, ctx.instr.type);
    }
};

class NarrowIntCastRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::CastSiNarrowChk || instr.op == Opcode::CastUiNarrowChk;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        const auto kind = ctx.instr.type.kind;
        if (kind != il::core::Type::Kind::I16 && kind != il::core::Type::Kind::I32)
            return fail(ctx, "narrowing cast result must be i16 or i32");
        return checkUnary(ctx, il::core::Type::Kind::I64, ctx.instr.type);
    }
};

class IdxChkRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::IdxChk;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkIdxChk(ctx);
    }
};

class ZextRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::Zext1;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkUnary(ctx, il::core::Type::Kind::I1, il::core::Type(il::core::Type::Kind::I64));
    }
};

class TruncRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::Trunc1;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkUnary(ctx, il::core::Type::Kind::I64, il::core::Type(il::core::Type::Kind::I1));
    }
};

class MemoryRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        switch (ctx.instr.op)
        {
            case Opcode::Alloca:
                return checkAlloca(ctx);
            case Opcode::GEP:
                return checkGEP(ctx);
            case Opcode::Load:
                return checkLoad(ctx);
            case Opcode::Store:
                return checkStore(ctx);
            case Opcode::AddrOf:
                return checkAddrOf(ctx);
            default:
                break;
        }
        return fail(ctx, "unsupported opcode");
    }

  private:
    static constexpr std::array<Opcode, 5> kOpcodes{
        Opcode::Alloca, Opcode::GEP, Opcode::Load, Opcode::Store, Opcode::AddrOf,
    };
};

class ConstRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::ConstStr || instr.op == Opcode::ConstNull;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        if (ctx.instr.op == Opcode::ConstStr)
            return checkConstStr(ctx);
        return checkConstNull(ctx);
    }
};

class CallRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::Call;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkCall(ctx);
    }
};

class TrapRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        switch (ctx.instr.op)
        {
            case Opcode::TrapKind:
                return checkTrapKind(ctx);
            case Opcode::TrapFromErr:
                return checkTrapFromErr(ctx);
            case Opcode::TrapErr:
                return checkTrapErr(ctx);
            default:
                break;
        }
        return fail(ctx, "unsupported opcode");
    }

  private:
    static constexpr std::array<Opcode, 3> kOpcodes{Opcode::TrapKind, Opcode::TrapFromErr, Opcode::TrapErr};
};

class ErrorGetFieldRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        if (auto result = expectAllOperandType(ctx, il::core::Type::Kind::Error); !result)
            return result;
        ctx.types.recordResult(ctx.instr, il::core::Type(il::core::Type::Kind::I32));
        return {};
    }

  private:
    static constexpr std::array<Opcode, 3> kOpcodes{Opcode::ErrGetKind, Opcode::ErrGetCode, Opcode::ErrGetLine};
};

class ErrorGetIpRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        return instr.op == Opcode::ErrGetIp;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        if (auto result = expectAllOperandType(ctx, il::core::Type::Kind::Error); !result)
            return result;
        ctx.types.recordResult(ctx.instr, il::core::Type(il::core::Type::Kind::I64));
        return {};
    }
};

class ResumeRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return expectAllOperandType(ctx, il::core::Type::Kind::ResumeTok);
    }

  private:
    static constexpr std::array<Opcode, 3> kOpcodes{Opcode::ResumeSame, Opcode::ResumeNext, Opcode::ResumeLabel};
};

class EhRule final : public Rule
{
  public:
    bool applies(const il::core::Instr &instr) const override
    {
        for (Opcode opcode : kOpcodes)
        {
            if (instr.op == opcode)
                return true;
        }
        return false;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkDefault(ctx);
    }

  private:
    static constexpr std::array<Opcode, 3> kOpcodes{Opcode::EhPush, Opcode::EhPop, Opcode::EhEntry};
};

class DefaultRule final : public Rule
{
  public:
    bool applies(const il::core::Instr & /*instr*/) const override
    {
        return true;
    }

    Status check(const VerifyCtx &ctx) const override
    {
        return checkDefault(ctx);
    }
};

const std::array<const Rule *, 20> &ruleRegistry()
{
    static const LegacyArithmeticRule legacyArithmeticRule;
    static const ForbiddenArithmeticRule forbiddenArithmeticRule;
    static const BinaryI64Rule binaryI64Rule;
    static const IntegerCompareRule integerCompareRule;
    static const FloatCompareRule floatCompareRule;
    static const SiToFpRule siToFpRule;
    static const CastFpToIntRule castFpToIntRule;
    static const NarrowIntCastRule narrowIntCastRule;
    static const IdxChkRule idxChkRule;
    static const ZextRule zextRule;
    static const TruncRule truncRule;
    static const MemoryRule memoryRule;
    static const ConstRule constRule;
    static const CallRule callRule;
    static const TrapRule trapRule;
    static const ErrorGetFieldRule errorGetFieldRule;
    static const ErrorGetIpRule errorGetIpRule;
    static const ResumeRule resumeRule;
    static const EhRule ehRule;
    static const DefaultRule defaultRule;

    static const std::array<const Rule *, 20> rules{
        &legacyArithmeticRule, &forbiddenArithmeticRule, &binaryI64Rule,      &integerCompareRule,
        &floatCompareRule,     &siToFpRule,              &castFpToIntRule,    &narrowIntCastRule,
        &idxChkRule,           &zextRule,                &truncRule,          &memoryRule,
        &constRule,            &callRule,                &trapRule,           &errorGetFieldRule,
        &errorGetIpRule,       &resumeRule,              &ehRule,             &defaultRule,
    };

    return rules;
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

/// @brief Perform full semantic verification for a single instruction.
/// @details Combines metadata-driven checks with opcode-specific handlers to
///          ensure operands, results, and side effects obey the IL specification
///          and runtime conventions.
/// @param ctx Verification context that supplies type inference, extern data,
///        and diagnostic sinks.
/// @return Empty success or a diagnostic when verification fails.
Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const bool isLegacyArithmetic = lookupLegacyArithmeticProps(ctx.instr).has_value();
    if (!isLegacyArithmetic)
    {
        const auto &info = il::core::getOpcodeInfo(ctx.instr.op);
        if (auto result = checkWithInfo(ctx, info); !result)
            return result;
    }

    for (const Rule *rule : ruleRegistry())
    {
        if (!rule->applies(ctx.instr))
            continue;
        return rule->check(ctx);
    }

    return fail(ctx, "unsupported opcode");
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
