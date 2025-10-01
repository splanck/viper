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
#include "support/diag_expected.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <limits>
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

    if (instr.labels.size() != info.numSuccessors)
    {
        std::ostringstream ss;
        ss << "expected " << static_cast<unsigned>(info.numSuccessors) << " successor";
        if (info.numSuccessors != 1)
            ss << 's';
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, ss.str()))};
    }

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
Expected<void> expectAllOperandType(const Function &fn,
                                    const BasicBlock &bb,
                                    const Instr &instr,
                                    TypeInference &types,
                                    Type::Kind kind)
{
    for (const auto &op : instr.operands)
    {
        if (types.valueType(op).kind != kind)
        {
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};
        }
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
Expected<void> checkAlloca_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types,
                             DiagSink &sink)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "missing size operand"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::I64)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "size must be i64"))};

    if (instr.operands[0].kind == Value::Kind::ConstInt)
    {
        long long sz = instr.operands[0].i64;
        if (sz < 0)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "negative alloca size"))};
        if (sz > (1LL << 20))
            emitWarning(fn, bb, instr, "huge alloca", sink);
    }

    types.recordResult(instr, Type(Type::Kind::Ptr));
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
Expected<void> checkBinary_E(const Function &fn,
                             const BasicBlock &bb,
                             const Instr &instr,
                             TypeInference &types,
                             Type::Kind operandKind,
                             Type resultType)
{
    if (instr.operands.size() < 2)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (auto result = expectAllOperandType(fn, bb, instr, types, operandKind); !result)
        return result;

    types.recordResult(instr, resultType);
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

    auto fitsInKind = [](long long value, Type::Kind kind) {
        switch (kind)
        {
            case Type::Kind::I16:
                return value >= std::numeric_limits<int16_t>::min() &&
                       value <= std::numeric_limits<int16_t>::max();
            case Type::Kind::I32:
                return value >= std::numeric_limits<int32_t>::min() &&
                       value <= std::numeric_limits<int32_t>::max();
            default:
                return false;
        }
    };

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
                if (fitsInKind(value.i64, Type::Kind::I16))
                    return Type::Kind::I16;
                if (fitsInKind(value.i64, Type::Kind::I32))
                    return Type::Kind::I32;
                return Expected<Type::Kind>{makeError(instr.loc,
                                                      formatInstrDiag(fn,
                                                                      bb,
                                                                      instr,
                                                                      "constant out of range for idx.chk"))};
            }
            if (!fitsInKind(value.i64, expectedKind))
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
Expected<void> checkUnary_E(const Function &fn,
                            const BasicBlock &bb,
                            const Instr &instr,
                            TypeInference &types,
                            Type::Kind operandKind,
                            Type resultType)
{
    if (instr.operands.empty())
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (types.valueType(instr.operands[0]).kind != operandKind)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};

    types.recordResult(instr, resultType);
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

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr ||
        types.valueType(instr.operands[1]).kind != Type::Kind::I64)
    {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "operand type mismatch"))};
    }

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
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "invalid operand count"))};

    if (instr.type.kind == Type::Kind::Void)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "void load type"))};

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

    if (instr.type.kind == Type::Kind::Void)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "void store type"))};

    if (types.valueType(instr.operands[0]).kind != Type::Kind::Ptr)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "pointer type mismatch"))};

    Type valueTy = types.valueType(instr.operands[1]);
    bool isBoolConst = instr.type.kind == Type::Kind::I1 && instr.operands[1].kind == Value::Kind::ConstInt;
    if (isBoolConst)
    {
        long long v = instr.operands[1].i64;
        if (v != 0 && v != 1)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "boolean store expects 0 or 1"))};
    }
    else if (valueTy.kind != instr.type.kind)
    {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "value type mismatch"))};
    }

    [[maybe_unused]] size_t sz = TypeInference::typeSize(instr.type.kind);
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
    types.recordResult(instr, Type(Type::Kind::Ptr));
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
Expected<void> checkCall_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const Extern *> &externs,
                           const std::unordered_map<std::string, const Function *> &funcs,
                           TypeInference &types)
{
    if (auto result = checkRuntimeArrayCall(fn, bb, instr, types); !result)
        return result;

    const Extern *sig = nullptr;
    const Function *fnSig = nullptr;
    if (auto it = externs.find(instr.callee); it != externs.end())
        sig = it->second;
    else if (auto itF = funcs.find(instr.callee); itF != funcs.end())
        fnSig = itF->second;

    if (!sig && !fnSig)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "unknown callee @" + instr.callee))};

    size_t paramCount = sig ? sig->params.size() : fnSig->params.size();
    if (instr.operands.size() != paramCount)
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg count mismatch"))};

    for (size_t i = 0; i < paramCount; ++i)
    {
        Type expected = sig ? sig->params[i] : fnSig->params[i].type;
        if (types.valueType(instr.operands[i]).kind != expected.kind)
            return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, "call arg type mismatch"))};
    }

    if (instr.result)
    {
        Type ret = sig ? sig->retType : fnSig->retType;
        types.recordResult(instr, ret);
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
Expected<void> verifyInstruction_impl(const Function &fn,
                                      const BasicBlock &bb,
                                      const Instr &instr,
                                      const std::unordered_map<std::string, const Extern *> &externs,
                                      const std::unordered_map<std::string, const Function *> &funcs,
                                      TypeInference &types,
                                      DiagSink &sink)
{
    const auto rejectUnchecked = [&](std::string_view message) {
        return Expected<void>{makeError(instr.loc, formatInstrDiag(fn, bb, instr, message))};
    };

    switch (instr.op)
    {
        case Opcode::Alloca:
            return checkAlloca_E(fn, bb, instr, types, sink);
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
        case Opcode::IAddOvf:
        case Opcode::ISubOvf:
        case Opcode::IMulOvf:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I64));
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::F64));
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
            return checkBinary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::FCmpEQ:
        case Opcode::FCmpNE:
        case Opcode::FCmpLT:
        case Opcode::FCmpLE:
        case Opcode::FCmpGT:
        case Opcode::FCmpGE:
            return checkBinary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::I1));
        case Opcode::Sitofp:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::F64));
        case Opcode::Fptosi:
            return rejectUnchecked(
                "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)");
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::F64, Type(Type::Kind::I64));
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I64));
        case Opcode::IdxChk:
            return checkIdxChk_E(fn, bb, instr, types);
        case Opcode::Zext1:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I1, Type(Type::Kind::I64));
        case Opcode::Trunc1:
            return checkUnary_E(fn, bb, instr, types, Type::Kind::I64, Type(Type::Kind::I1));
        case Opcode::GEP:
            return checkGEP_E(fn, bb, instr, types);
        case Opcode::Load:
            return checkLoad_E(fn, bb, instr, types);
        case Opcode::Store:
            return checkStore_E(fn, bb, instr, types);
        case Opcode::AddrOf:
            return checkAddrOf_E(fn, bb, instr, types);
        case Opcode::ConstStr:
            return checkConstStr_E(fn, bb, instr, types);
        case Opcode::ConstNull:
            return checkConstNull_E(instr, types);
        case Opcode::Call:
            return checkCall_E(fn, bb, instr, externs, funcs, types);
        case Opcode::TrapKind:
            return expectAllOperandType(fn, bb, instr, types, Type::Kind::I64);
        case Opcode::TrapFromErr:
            return checkTrapFromErr_E(fn, bb, instr, types);
        case Opcode::TrapErr:
            return expectAllOperandType(fn, bb, instr, types, Type::Kind::Error);
        case Opcode::ErrGetKind:
        case Opcode::ErrGetCode:
        case Opcode::ErrGetLine:
        {
            if (auto result = expectAllOperandType(fn, bb, instr, types, Type::Kind::Error); !result)
                return result;
            types.recordResult(instr, Type(Type::Kind::I32));
            return {};
        }
        case Opcode::ErrGetIp:
        {
            if (auto result = expectAllOperandType(fn, bb, instr, types, Type::Kind::Error); !result)
                return result;
            types.recordResult(instr, Type(Type::Kind::I64));
            return {};
        }
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return expectAllOperandType(fn, bb, instr, types, Type::Kind::ResumeTok);
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
            return checkDefault_E(instr, types);
        default:
            return checkDefault_E(instr, types);
    }
}

} // namespace

Expected<void> verifyOpcodeSignature_E(const Function &fn,
                                        const BasicBlock &bb,
                                        const Instr &instr)
{
    return verifyOpcodeSignature_impl(fn, bb, instr);
}

Expected<void> verifyInstruction_E(const Function &fn,
                                    const BasicBlock &bb,
                                    const Instr &instr,
                                    const std::unordered_map<std::string, const Extern *> &externs,
                                    const std::unordered_map<std::string, const Function *> &funcs,
                                    TypeInference &types,
                                    DiagSink &sink)
{
    return verifyInstruction_impl(fn, bb, instr, externs, funcs, types, sink);
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
