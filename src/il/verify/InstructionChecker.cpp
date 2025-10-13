// File: src/il/verify/InstructionChecker.cpp
// Purpose: Implements helpers that validate non-control IL instructions.
// Key invariants: Relies on TypeInference to keep operand types consistent.
// Ownership/Lifetime: Functions operate on caller-provided structures.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/verify/InstructionChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/TypeInference.hpp"
#include "il/verify/VerifyCtx.hpp"
#include "il/verify/VerifierTable.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Diag;
using il::support::Expected;
using il::support::Severity;
using il::support::makeError;

bool fitsInIntegerKind(long long value, Type::Kind kind)
{
    switch (kind)
    {
        case Type::Kind::I1:
            return value == 0 || value == 1;
        case Type::Kind::I16:
            return value >= std::numeric_limits<int16_t>::min() &&
                   value <= std::numeric_limits<int16_t>::max();
        case Type::Kind::I32:
            return value >= std::numeric_limits<int32_t>::min() &&
                   value <= std::numeric_limits<int32_t>::max();
        case Type::Kind::I64:
            return true;
        default:
            return false;
    }
}

Expected<void> checkBinary_E(const VerifyCtx &ctx,
                             Type::Kind operandKind,
                             Type resultType);

Expected<void> checkUnary_E(const VerifyCtx &ctx,
                            Type::Kind operandKind,
                            Type resultType);


std::optional<Type::Kind> kindFromClass(TypeClass typeClass)
{
    switch (typeClass)
    {
        case TypeClass::Void:
            return Type::Kind::Void;
        case TypeClass::I1:
            return Type::Kind::I1;
        case TypeClass::I16:
            return Type::Kind::I16;
        case TypeClass::I32:
            return Type::Kind::I32;
        case TypeClass::I64:
            return Type::Kind::I64;
        case TypeClass::F64:
            return Type::Kind::F64;
        case TypeClass::Ptr:
            return Type::Kind::Ptr;
        case TypeClass::Str:
            return Type::Kind::Str;
        case TypeClass::Error:
            return Type::Kind::Error;
        case TypeClass::ResumeTok:
            return Type::Kind::ResumeTok;
        case TypeClass::None:
            return std::nullopt;
        case TypeClass::InstrType:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Type> typeFromClass(TypeClass typeClass)
{
    if (typeClass == TypeClass::InstrType)
        return std::nullopt;
    if (auto kind = kindFromClass(typeClass))
        return Type(*kind);
    return std::nullopt;
}

std::optional<Type::Kind> kindFromCategory(TypeCategory category)
{
    switch (category)
    {
        case TypeCategory::Void:
            return Type::Kind::Void;
        case TypeCategory::I1:
            return Type::Kind::I1;
        case TypeCategory::I16:
            return Type::Kind::I16;
        case TypeCategory::I32:
            return Type::Kind::I32;
        case TypeCategory::I64:
            return Type::Kind::I64;
        case TypeCategory::F64:
            return Type::Kind::F64;
        case TypeCategory::Ptr:
            return Type::Kind::Ptr;
        case TypeCategory::Str:
            return Type::Kind::Str;
        case TypeCategory::Error:
            return Type::Kind::Error;
        case TypeCategory::ResumeTok:
            return Type::Kind::ResumeTok;
        case TypeCategory::None:
        case TypeCategory::Any:
        case TypeCategory::InstrType:
        case TypeCategory::Dynamic:
            return std::nullopt;
    }
    return std::nullopt;
}

Expected<void> checkWithInfo(const VerifyCtx &ctx, const il::core::OpcodeInfo &info)
{
    const Instr &instr = ctx.instr;

    const size_t operandCount = instr.operands.size();
    const bool variadicOperands = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadicOperands && operandCount > info.numOperandsMax))
    {
        std::ostringstream ss;
        if (info.numOperandsMin == info.numOperandsMax && !variadicOperands)
        {
            ss << "expected " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else if (variadicOperands)
        {
            ss << "expected at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else
        {
            ss << "expected between " << static_cast<unsigned>(info.numOperandsMin) << " and "
               << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        return Expected<void>{makeError(instr.loc, formatInstrDiag(ctx.fn, ctx.block, instr, ss.str()))};
    }

    for (size_t index = 0; index < instr.operands.size() && index < info.operandTypes.size(); ++index)
    {
        const TypeCategory category = info.operandTypes[index];
        if (category == TypeCategory::None || category == TypeCategory::Any || category == TypeCategory::Dynamic)
            continue;

        Type::Kind expectedKind;
        if (category == TypeCategory::InstrType)
        {
            if (instr.type.kind == Type::Kind::Void)
            {
                return Expected<void>{makeError(instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                instr,
                                                                "instruction type must be non-void"))};
            }
            expectedKind = instr.type.kind;
        }
        else if (auto mapped = kindFromCategory(category))
        {
            expectedKind = *mapped;
        }
        else
        {
            continue;
        }

        const Value &operand = instr.operands[index];
        if (operand.kind == Value::Kind::ConstInt)
        {
            switch (expectedKind)
            {
                case Type::Kind::I1:
                    if (!fitsInIntegerKind(operand.i64, expectedKind))
                    {
                        std::ostringstream ss;
                        ss << "operand " << index << " constant out of range for i1";
                        return Expected<void>{makeError(instr.loc,
                                                        formatInstrDiag(ctx.fn,
                                                                        ctx.block,
                                                                        instr,
                                                                        ss.str()))};
                    }
                    continue;
                case Type::Kind::I16:
                case Type::Kind::I32:
                case Type::Kind::I64:
                    if (!fitsInIntegerKind(operand.i64, expectedKind))
                    {
                        std::ostringstream ss;
                        ss << "operand " << index << " constant out of range for "
                           << kindToString(expectedKind);
                        return Expected<void>{makeError(instr.loc,
                                                        formatInstrDiag(ctx.fn,
                                                                        ctx.block,
                                                                        instr,
                                                                        ss.str()))};
                    }
                    continue;
                default:
                    break;
            }
        }

        bool missing = false;
        const Type actual = ctx.types.valueType(operand, &missing);
        if (missing)
        {
            std::ostringstream ss;
            ss << "operand " << index << " type is unknown";
            return Expected<void>{makeError(instr.loc, formatInstrDiag(ctx.fn, ctx.block, instr, ss.str()))};
        }

        if (actual.kind != expectedKind)
        {
            std::ostringstream ss;
            if (expectedKind == Type::Kind::Ptr)
            {
                ss << "pointer type mismatch";
            }
            else
            {
                ss << "operand " << index << " must be " << kindToString(expectedKind);
            }
            return Expected<void>{makeError(instr.loc, formatInstrDiag(ctx.fn, ctx.block, instr, ss.str()))};
        }
    }

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
                return Expected<void>{makeError(instr.loc,
                                                formatInstrDiag(ctx.fn, ctx.block, instr, "unexpected result"))};
            return {};
        case ResultArity::One:
            if (!hasResult)
                return Expected<void>{makeError(instr.loc,
                                                formatInstrDiag(ctx.fn, ctx.block, instr, "missing result"))};
            break;
        case ResultArity::Optional:
            if (!hasResult)
                return {};
            break;
    }

    if (info.resultType == TypeCategory::InstrType)
    {
        if (instr.op != Opcode::IdxChk && instr.type.kind == Type::Kind::Void)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(ctx.fn, ctx.block, instr, "instruction type must be non-void"))};
        }
    }
    else if (auto expectedKind = kindFromCategory(info.resultType))
    {
        const bool skipResultTypeCheck =
            ctx.instr.op == Opcode::CastFpToSiRteChk || ctx.instr.op == Opcode::CastFpToUiRteChk ||
            ctx.instr.op == Opcode::CastSiNarrowChk || ctx.instr.op == Opcode::CastUiNarrowChk;

        if (!skipResultTypeCheck && instr.type.kind != *expectedKind)
        {
            std::ostringstream ss;
            ss << "result type must be " << kindToString(*expectedKind);
            return Expected<void>{makeError(instr.loc, formatInstrDiag(ctx.fn, ctx.block, instr, ss.str()))};
        }
    }

    return {};
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
            return checkUnary_E(ctx, *operandKind, *resultType);
        }
        case 2:
        {
            const auto operandKind = kindFromClass(props.operands);
            const auto resultType = typeFromClass(props.result);
            assert(operandKind && resultType);
            return checkBinary_E(ctx, *operandKind, *resultType);
        }
        default:
            break;
    }

    assert(false && "unsupported verifier table arity");
    return {};
}

enum class RuntimeArrayCallee
{
    None,
    New,
    Len,
    Get,
    Set,
    Resize,
    Retain,
    Release
};

RuntimeArrayCallee classifyRuntimeArrayCallee(std::string_view callee)
{
    if (callee == "rt_arr_i32_new")
        return RuntimeArrayCallee::New;
    if (callee == "rt_arr_i32_len")
        return RuntimeArrayCallee::Len;
    if (callee == "rt_arr_i32_get")
        return RuntimeArrayCallee::Get;
    if (callee == "rt_arr_i32_set")
        return RuntimeArrayCallee::Set;
    if (callee == "rt_arr_i32_resize")
        return RuntimeArrayCallee::Resize;
    if (callee == "rt_arr_i32_retain")
        return RuntimeArrayCallee::Retain;
    if (callee == "rt_arr_i32_release")
        return RuntimeArrayCallee::Release;
    return RuntimeArrayCallee::None;
}

Expected<void> checkRuntimeArrayCall(const Function &fn,
                                     const BasicBlock &bb,
                                     const Instr &instr,
                                     TypeInference &types)
{
    const RuntimeArrayCallee calleeKind = classifyRuntimeArrayCallee(instr.callee);
    if (calleeKind == RuntimeArrayCallee::None)
        return {};

    const auto fail = [&](std::string_view message) {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, message))};
    };

    const auto requireArgCount = [&](size_t expected) -> Expected<void> {
        if (instr.operands.size() == expected)
            return {};

        std::ostringstream ss;
        ss << "expected " << expected << " argument";
        if (expected != 1)
            ss << 's';
        ss << " to @" << instr.callee;
        return fail(ss.str());
    };

    const auto requireOperandType = [&](size_t index, Type::Kind expected, std::string_view role) {
        bool missing = false;
        const Type actual = types.valueType(instr.operands[index], &missing);
        if (missing)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " " << role << " operand has unknown type";
            return fail(ss.str());
        }
        if (actual.kind != expected)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " " << role << " operand must be " << kindToString(expected);
            return fail(ss.str());
        }
        return Expected<void>{};
    };

    const auto requireResultType = [&](Type::Kind expected) -> Expected<void> {
        if (!instr.result)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " must produce " << kindToString(expected) << " result";
            return fail(ss.str());
        }
        if (instr.type.kind != expected)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " result must be " << kindToString(expected);
            return fail(ss.str());
        }
        return Expected<void>{};
    };

    const auto requireNoResult = [&]() -> Expected<void> {
        if (instr.result)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " must not produce a result";
            return fail(ss.str());
        }
        if (instr.type.kind != Type::Kind::Void)
        {
            std::ostringstream ss;
            ss << "@" << instr.callee << " result type must be void";
            return fail(ss.str());
        }
        return Expected<void>{};
    };

    switch (calleeKind)
    {
        case RuntimeArrayCallee::New:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::Len:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::Get:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::Set:
        {
            if (auto result = requireArgCount(3); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            if (auto result = requireOperandType(2, Type::Kind::I64, "value"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::Resize:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::Retain:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::Release:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::None:
            break;
    }

    return {};
}

/// @brief Append a warning diagnostic associated with @p instr.
/// @param fn Function supplying context for the diagnostic message.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction that prompted the warning.
/// @param message Human-readable warning text.
/// @param warnings Collection receiving deferred warning diagnostics.
void emitWarning(const Function &fn,
                 const BasicBlock &bb,
                 const Instr &instr,
                 std::string_view message,
                 DiagSink &sink)
{
    sink.report(Diag{Severity::Warning, formatInstrDiag(fn, bb, instr, message), instr.loc});
}

/// @brief Validate operand/result arity constraints against opcode metadata.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose signature is being checked.
/// @return Empty on success; otherwise an error diagnostic describing the
///         structural mismatch.
Expected<void> verifyOpcodeSignature_impl(const Function &fn,
                                          const BasicBlock &bb,
                                          const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);

    const bool hasResult = instr.result.has_value();
    switch (info.resultArity)
    {
        case ResultArity::None:
            if (hasResult)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unexpected result"))};
            break;
        case ResultArity::One:
            if (!hasResult)
                return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing result"))};
            break;
        case ResultArity::Optional:
            break;
    }

    const size_t operandCount = instr.operands.size();
    const bool variadic = isVariadicOperandCount(info.numOperandsMax);
    if (operandCount < info.numOperandsMin || (!variadic && operandCount > info.numOperandsMax))
    {
        std::ostringstream ss;
        if (info.numOperandsMin == info.numOperandsMax)
        {
            ss << "expected " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else if (variadic)
        {
            ss << "expected at least " << static_cast<unsigned>(info.numOperandsMin) << " operand";
            if (info.numOperandsMin != 1)
                ss << 's';
        }
        else
        {
            ss << "expected between " << static_cast<unsigned>(info.numOperandsMin) << " and "
               << static_cast<unsigned>(info.numOperandsMax) << " operands";
        }
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }

    const bool variadicSucc = isVariadicSuccessorCount(info.numSuccessors);
    if (variadicSucc)
    {
        if (instr.labels.empty())
        {
            std::ostringstream ss;
            ss << "expected at least 1 successor";
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
        }
    }
    else
    {
        if (instr.labels.size() != info.numSuccessors)
        {
            std::ostringstream ss;
            ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " successor";
            if (info.numSuccessors != 1)
                ss << 's';
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
        }
    }

    if (variadicSucc)
    {
        if (!instr.brArgs.empty() && instr.brArgs.size() != instr.labels.size())
        {
            std::ostringstream ss;
            ss << "expected branch argument bundle per successor or none";
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
        }
    }
    else
    {
        if (instr.brArgs.size() > info.numSuccessors)
        {
            std::ostringstream ss;
            ss << "expected at most " << static_cast<unsigned>(info.numSuccessors)
               << " branch argument bundle";
            if (info.numSuccessors != 1)
                ss << 's';
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
        }
        if (!instr.brArgs.empty() && instr.brArgs.size() != info.numSuccessors)
        {
            std::ostringstream ss;
            ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " branch argument bundle";
            if (info.numSuccessors != 1)
                ss << 's';
            ss << ", or none";
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
        }
    }

    return {};
}

/// @brief Require all operands of @p instr to resolve to the requested type
///        kind.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose operands are being validated.
/// @param types Type inference engine that answers operand queries.
/// @param kind Expected operand kind.
/// @return Empty on success; otherwise an error diagnostic when a mismatch is
///         observed.
Expected<void> expectAllOperandType(const VerifyCtx &ctx, Type::Kind kind)
{
    for (const auto &op : ctx.instr.operands)
    {
        if (ctx.types.valueType(op).kind != kind)
            return Expected<void>{makeError(ctx.instr.loc,
                                            formatInstrDiag(
                                                ctx.fn, ctx.block, ctx.instr, "operand type mismatch"))};
    }
    return {};
}

/// @brief Validate allocator instructions for operand and result correctness.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Alloca instruction being verified.
/// @param types Type inference engine for operand/result metadata.
/// @param warnings Warning sink for questionable but allowed patterns.
/// @return Empty on success; otherwise an error diagnostic describing the
///         violated constraint.
Expected<void> checkAlloca_E(const VerifyCtx &ctx)
{
    const Instr &instr = ctx.instr;
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, instr, "missing size operand"))};

    if (ctx.types.valueType(instr.operands[0]).kind != Type::Kind::I64)
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, instr, "size must be i64"))};

    if (instr.operands[0].kind == Value::Kind::ConstInt)
    {
        long long sz = instr.operands[0].i64;
        if (sz < 0)
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(ctx.fn, ctx.block, instr, "negative alloca size"))};
        if (sz > (1LL << 20))
            emitWarning(ctx.fn, ctx.block, instr, "huge alloca", ctx.diags);
    }

    ctx.types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify binary arithmetic and comparison instructions.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose operands and result are validated.
/// @param types Type inference engine that answers operand queries.
/// @param operandKind Expected kind shared by both operands.
/// @param resultType Result type recorded when validation succeeds.
/// @return Empty on success; otherwise an error diagnostic describing arity or
///         type mismatches.
Expected<void> checkBinary_E(const VerifyCtx &ctx,
                             Type::Kind operandKind,
                             Type resultType)
{
    const Instr &instr = ctx.instr;
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, instr, "invalid operand count"))};

    if (auto result = expectAllOperandType(ctx, operandKind); !result)
        return result;

    ctx.types.recordResult(instr, resultType);
    return {};
}

/// @brief Validate idx.chk range checks for operand width consistency.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr idx.chk instruction under validation.
/// @param types Type inference engine used for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing missing
///         operands or type mismatches.
Expected<void> checkIdxChk_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types)
{
    if (instr.operands.size() != 3)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    Type::Kind expectedKind = Type::Kind::Void;
    if (instr.type.kind == Type::Kind::I16 || instr.type.kind == Type::Kind::I32)
        expectedKind = instr.type.kind;

    const auto classifyOperand = [&](const Value &value) -> Expected<Type::Kind> {
        if (value.kind == Value::Kind::Temp)
        {
            Type::Kind kind = types.valueType(value).kind;
            if (kind == Type::Kind::Void)
            {
                return Expected<Type::Kind>{makeError(instr.loc,
                                                      formatInstrDiag(fn, bb, instr,
                                                                      "unknown temp in idx.chk"))};
            }
            return kind;
        }
        if (value.kind == Value::Kind::ConstInt)
        {
            if (expectedKind == Type::Kind::Void)
            {
                if (fitsInIntegerKind(value.i64, Type::Kind::I16))
                    return Type::Kind::I16;
                if (fitsInIntegerKind(value.i64, Type::Kind::I32))
                    return Type::Kind::I32;
                return Expected<Type::Kind>{makeError(instr.loc,
                                                      formatInstrDiag(fn,
                                                                      bb,
                                                                      instr,
                                                                      "constant out of range for idx.chk"))};
            }
            if (!fitsInIntegerKind(value.i64, expectedKind))
            {
                return Expected<Type::Kind>{makeError(instr.loc,
                                                      formatInstrDiag(fn,
                                                                      bb,
                                                                      instr,
                                                                      "constant out of range for idx.chk"))};
            }
            return expectedKind;
        }
        return Expected<Type::Kind>{makeError(instr.loc,
                                              formatInstrDiag(fn,
                                                              bb,
                                                              instr,
                                                              "operands must be i16 or i32"))};
    };

    for (size_t i = 0; i < instr.operands.size(); ++i)
    {
        auto kindResult = classifyOperand(instr.operands[i]);
        if (!kindResult)
            return Expected<void>{kindResult.error()};
        Type::Kind operandKind = kindResult.value();
        if (operandKind != Type::Kind::I16 && operandKind != Type::Kind::I32)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn,
                                                            bb,
                                                            instr,
                                                            "operands must be i16 or i32"))};
        }
        if (expectedKind == Type::Kind::Void)
        {
            expectedKind = operandKind;
        }
        else if (operandKind != expectedKind)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn,
                                                            bb,
                                                            instr,
                                                            "operands must share i16/i32 width"))};
        }
    }

    if (expectedKind != Type::Kind::I16 && expectedKind != Type::Kind::I32)
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn,
                                                        bb,
                                                        instr,
                                                        "operands must be i16 or i32"))};
    }

    if (instr.type.kind != Type::Kind::Void && instr.type.kind != expectedKind)
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn,
                                                        bb,
                                                        instr,
                                                        "result type annotation must match operand width"))};
    }

    types.recordResult(instr, Type(expectedKind));
    return {};
}

/// @brief Validate trap.from_err operands ensure i32 typing and range.
/// @param fn Function providing diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr trap.from_err instruction under validation.
/// @param types Type inference helper used for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing arity or typing issues.
Expected<void> checkTrapFromErr_E(const Function &fn,
                                  const BasicBlock &bb,
                                  const Instr &instr,
                                  TypeInference &types)
{
    if (instr.operands.size() != 1)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (instr.type.kind != Type::Kind::I32)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "trap.from_err expects i32 type"))};

    const auto &operand = instr.operands.front();
    if (operand.kind == Value::Kind::Temp)
    {
        if (types.valueType(operand).kind != Type::Kind::I32)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn, bb, instr, "trap.from_err operand must be i32"))};
        }
    }
    else if (operand.kind == Value::Kind::ConstInt)
    {
        if (operand.i64 < std::numeric_limits<int32_t>::min() || operand.i64 > std::numeric_limits<int32_t>::max())
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn, bb, instr,
                                                            "trap.from_err constant out of range"))};
        }
    }
    else
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn, bb, instr, "trap.from_err operand must be i32"))};
    }

    return {};
}

/// @brief Verify unary conversions and casts.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction whose single operand is validated.
/// @param types Type inference engine that answers operand queries.
/// @param operandKind Required operand kind.
/// @param resultType Result type recorded when validation succeeds.
/// @return Empty on success; otherwise an error diagnostic describing arity or
///         type mismatches.
Expected<void> checkUnary_E(const VerifyCtx &ctx,
                            Type::Kind operandKind,
                            Type resultType)
{
    const Instr &instr = ctx.instr;
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, instr, "invalid operand count"))};

    if (ctx.types.valueType(instr.operands[0]).kind != operandKind)
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, instr, "operand type mismatch"))};

    ctx.types.recordResult(instr, resultType);
    return {};
}

/// @brief Validate pointer arithmetic instructions (GEP).
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr GEP instruction under validation.
/// @param types Type inference engine used for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing missing
///         operands or pointer/index type mismatches.
Expected<void> checkGEP_E(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          TypeInference &types)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Validate load instructions for pointer and result type correctness.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Load instruction being verified.
/// @param types Type inference engine for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing arity,
///         pointer, or result type violations.
Expected<void> checkLoad_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           TypeInference &types)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing operand"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "pointer type mismatch"))};

    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
    types.recordResult(instr, instr.type);
    return {};
}

/// @brief Validate store instructions for pointer operand and value typing.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Store instruction being verified.
/// @param types Type inference engine for operand queries.
/// @return Empty on success; otherwise an error diagnostic describing arity,
///         pointer, or stored value violations.
Expected<void> checkStore_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            TypeInference &types)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    bool pointerMissing = false;
    const Type pointerTy = types.valueType(instr.operands[0], &pointerMissing);
    if (pointerMissing)
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn, bb, instr, "pointer operand type is unknown"))};
    }
    if (pointerTy.kind != Type::Kind::Ptr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "pointer type mismatch"))};

    bool isBoolConst = instr.type.kind == Type::Kind::I1 && instr.operands[1].kind == Value::Kind::ConstInt;
    if (isBoolConst)
    {
        long long v = instr.operands[1].i64;
        if (v != 0 && v != 1)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "boolean store expects 0 or 1"))};
    }
    else if (instr.operands[1].kind == Value::Kind::ConstInt &&
             (instr.type.kind == Type::Kind::I16 || instr.type.kind == Type::Kind::I32))
    {
        long long v = instr.operands[1].i64;
        long long min = instr.type.kind == Type::Kind::I16 ? std::numeric_limits<int16_t>::min()
                                                            : std::numeric_limits<int32_t>::min();
        long long max = instr.type.kind == Type::Kind::I16 ? std::numeric_limits<int16_t>::max()
                                                            : std::numeric_limits<int32_t>::max();
        if (v < min || v > max)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn, bb, instr, "value out of range for store type"))};
        }
    }
    return {};
}

/// @brief Validate AddrOf instructions address globals and produce pointers.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr AddrOf instruction being verified.
/// @param types Type inference engine for result recording.
/// @return Empty on success; otherwise an error diagnostic when the operand is
///         not a global address.
Expected<void> checkAddrOf_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types)
{
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand must be global"))};

    types.recordResult(instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Validate ConstStr instructions reference known string globals.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr ConstStr instruction being verified.
/// @param types Type inference engine for result recording.
/// @return Empty on success; otherwise an error diagnostic when the operand is
///         not a string global.
Expected<void> checkConstStr_E(const Function &fn,
                               const BasicBlock &bb,
                               const Instr &instr,
                               TypeInference &types)
{
    if (instr.operands.size() != 1 || instr.operands[0].kind != Value::Kind::GlobalAddr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unknown string global"))};

    types.recordResult(instr, Type(Type::Kind::Str));
    return {};
}

/// @brief Record the result type for ConstNull instructions.
/// @param instr ConstNull instruction being validated.
/// @param types Type inference engine for result recording.
/// @return Always empty because ConstNull has no failure modes.
Expected<void> checkConstNull_E(const Instr &instr, TypeInference &types)
{
    Type resultType = instr.type;
    switch (resultType.kind)
    {
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            break;
        default:
            resultType = Type(Type::Kind::Ptr);
            break;
    }

    types.recordResult(instr, resultType);
    return {};
}

/// @brief Validate direct calls against extern or function signatures.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Call instruction being verified.
/// @param externs Table of known extern declarations.
/// @param funcs Table of known function definitions.
/// @param types Type inference engine for operand queries and result recording.
/// @return Empty on success; otherwise an error diagnostic describing missing
///         callees, arity mismatches, or argument type violations.
Expected<void> checkCall_E(const VerifyCtx &ctx)
{
    const Function &fn = ctx.fn;
    const BasicBlock &bb = ctx.block;
    const Instr &instr = ctx.instr;

    if (auto result = checkRuntimeArrayCall(fn, bb, instr, ctx.types); !result)
        return result;

    const Extern *sig = nullptr;
    const Function *fnSig = nullptr;
    if (auto it = ctx.externs.find(instr.callee); it != ctx.externs.end())
        sig = it->second;
    else if (auto itF = ctx.functions.find(instr.callee); itF != ctx.functions.end())
        fnSig = itF->second;

    if (!sig && !fnSig)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unknown callee @" + instr.callee))};

    size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
    if (instr.operands.size() != paramCount)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg count mismatch"))};

    for (size_t i = 0; i < paramCount; ++i)
    {
        Type expected = sig ? sig->params[i] : fnSig->params[i].type;
        if (ctx.types.valueType(instr.operands[i]).kind != expected.kind)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg type mismatch"))};
    }

    if (instr.result)
    {
        Type ret = sig ? sig->retType : fnSig->retType;
        ctx.types.recordResult(instr, ret);
    }

    return {};
}

/// @brief Default validator that records the declared result type.
/// @param instr Instruction being validated.
/// @param types Type inference engine for result recording.
/// @return Always empty because structural checks handle failures.
Expected<void> checkDefault_E(const Instr &instr, TypeInference &types)
{
    types.recordResult(instr, instr.type);
    return {};
}

/// @brief Dispatch opcode-specific validation for non-control instructions.
/// @param fn Function supplying diagnostic context.
/// @param bb Basic block containing the instruction.
/// @param instr Instruction being verified.
/// @param externs Table of known extern declarations.
/// @param funcs Table of known function definitions.
/// @param types Type inference engine for operand queries and result recording.
/// @param sink Diagnostic sink receiving warnings emitted during validation.
/// @return Empty on success; otherwise an error diagnostic describing the
///         violated rule.
Expected<void> verifyInstruction_impl(const VerifyCtx &ctx)
{
    const auto rejectUnchecked = [&](std::string_view message) {
        return Expected<void>{makeError(ctx.instr.loc,
                                        formatInstrDiag(ctx.fn, ctx.block, ctx.instr, message))};
    };

    const auto &info = il::core::getOpcodeInfo(ctx.instr.op);
    if (auto result = checkWithInfo(ctx, info); !result)
        return result;

    const auto props = lookup(ctx.instr.op);
    const bool hasLegacyArithmeticProps =
        props && props->arity > 0 && props->arity <= 2 && kindFromClass(props->operands).has_value() &&
        typeFromClass(props->result).has_value();

    if (hasLegacyArithmeticProps)
        return checkWithProps(ctx, *props);

    switch (ctx.instr.op)
    {
        case Opcode::Alloca:
            return checkAlloca_E(ctx);
        case Opcode::Add:
            return rejectUnchecked("signed integer add must use iadd.ovf (traps on overflow)");
        case Opcode::Sub:
            return rejectUnchecked("signed integer sub must use isub.ovf (traps on overflow)");
        case Opcode::Mul:
            return rejectUnchecked("signed integer mul must use imul.ovf (traps on overflow)");
        case Opcode::SDiv:
            return rejectUnchecked(
                "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)");
        case Opcode::UDiv:
            return rejectUnchecked("unsigned division must use udiv.chk0 (traps on divide-by-zero)");
        case Opcode::SRem:
            return rejectUnchecked(
                "signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)");
        case Opcode::URem:
            return rejectUnchecked(
                "unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)");
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            return checkBinary_E(ctx, Type::Kind::I64, Type(Type::Kind::I64));
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
            return checkBinary_E(ctx, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return checkBinary_E(ctx, Type::Kind::F64, Type(Type::Kind::I1));
        case Opcode::Sitofp:
            return checkUnary_E(ctx, Type::Kind::I64, Type(Type::Kind::F64));
        case Opcode::Fptosi:
            return rejectUnchecked(
                "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)");
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        {
            if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32 &&
                ctx.instr.type.kind != Type::Kind::I64)
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "cast result must be i16, i32, or i64"))};
            }
            return checkUnary_E(ctx, Type::Kind::F64, ctx.instr.type);
        }
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
        {
            if (ctx.instr.type.kind != Type::Kind::I16 && ctx.instr.type.kind != Type::Kind::I32)
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "narrowing cast result must be i16 or i32"))};
            }
            return checkUnary_E(ctx, Type::Kind::I64, ctx.instr.type);
        }
        case Opcode::IdxChk:
            return checkIdxChk_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::Zext1:
            return checkUnary_E(ctx, Type::Kind::I1, Type(Type::Kind::I64));
        case Opcode::Trunc1:
            return checkUnary_E(ctx, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::GEP:
            return checkGEP_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::Load:
            return checkLoad_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::Store:
            return checkStore_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::AddrOf:
            return checkAddrOf_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::ConstStr:
            return checkConstStr_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::ConstNull:
            return checkConstNull_E(ctx.instr, ctx.types);
        case Opcode::Call:
            return checkCall_E(ctx);
        case Opcode::TrapKind:
        {
            if (!ctx.instr.operands.empty())
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "trap.kind takes no operands"))};
            }
            ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
            return {};
        }
        case Opcode::TrapFromErr:
            return checkTrapFromErr_E(ctx.fn, ctx.block, ctx.instr, ctx.types);
        case Opcode::TrapErr:
        {
            if (ctx.instr.operands.size() != 2)
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "trap.err expects code and text operands"))};
            }
            const auto codeType = ctx.types.valueType(ctx.instr.operands[0]).kind;
            if (codeType != Type::Kind::I32)
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "trap.err code must be i32"))};
            }
            const auto textType = ctx.types.valueType(ctx.instr.operands[1]).kind;
            if (textType != Type::Kind::Str)
            {
                return Expected<void>{makeError(ctx.instr.loc,
                                                formatInstrDiag(ctx.fn,
                                                                ctx.block,
                                                                ctx.instr,
                                                                "trap.err text must be str"))};
            }
            ctx.types.recordResult(ctx.instr, Type(Type::Kind::Error));
            return {};
        }
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetLine:
        {
            if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
                return result;
            ctx.types.recordResult(ctx.instr, Type(Type::Kind::I32));
            return {};
        }
        case Opcode::ErrGetIp:
        {
            if (auto result = expectAllOperandType(ctx, Type::Kind::Error); !result)
                return result;
            ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
            return {};
        }
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return expectAllOperandType(ctx, Type::Kind::ResumeTok);
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
            return checkDefault_E(ctx.instr, ctx.types);
        default:
            return checkDefault_E(ctx.instr, ctx.types);
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

Expected<void> verifyInstruction_E(const Function &fn,
                                    const BasicBlock &bb,
                                    const Instr &instr,
                                    const std::unordered_map<std::string, const Extern *> &externs,
                                    const std::unordered_map<std::string, const Function *> &funcs,
                                    TypeInference &types,
                                    DiagSink &sink)
{
    VerifyCtx ctx{sink, types, externs, funcs, fn, bb, instr};
    return verifyInstruction_impl(ctx);
}

Expected<void> verifyOpcodeSignature_E(const Function &fn,
                                       const BasicBlock &bb,
                                       const Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

bool verifyOpcodeSignature(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           std::ostream &err)
{
    if (auto result = verifyOpcodeSignature_E(fn, bb, instr); !result)
    {
        il::support::printDiag(result.error(), err);
        return false;
    }
    return true;
}

bool verifyInstruction(const Function &fn,
                       const BasicBlock &bb,
                       const Instr &instr,
                       const std::unordered_map<std::string, const Extern *> &externs,
                       const std::unordered_map<std::string, const Function *> &funcs,
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

Expected<void> verifyOpcodeSignature_expected(const Function &fn,
                                               const BasicBlock &bb,
                                               const Instr &instr)
{
    return verifyOpcodeSignature_E(fn, bb, instr);
}

Expected<void> verifyInstruction_expected(const Function &fn,
                                          const BasicBlock &bb,
                                          const Instr &instr,
                                          const std::unordered_map<std::string, const Extern *> &externs,
                                          const std::unordered_map<std::string, const Function *> &funcs,
                                          TypeInference &types,
                                          DiagSink &sink)
{
    return verifyInstruction_E(fn, bb, instr, externs, funcs, types, sink);
}

} // namespace il::verify
