//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the verifier utilities that validate runtime helper invocations
// and generic call instructions.  The routines codify the operand counts,
// operand types, and result requirements for helpers declared in
// RuntimeSignatures.hpp so front-end generated IL is checked against the same
// ABI used by the VM and native code paths.
//
//===----------------------------------------------------------------------===//

#include "il/verify/InstructionCheckerShared.hpp"

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/verify/TypeInference.hpp"

#include <limits>
#include <sstream>
#include <string_view>

namespace il::verify::checker
{

using il::core::Extern;
using il::core::Function;
using il::core::Type;
using il::core::kindToString;
using il::support::Expected;

namespace
{

/// @brief Enumerates the runtime array helper routines understood by the
///        verifier.
enum class RuntimeArrayCallee
{
    None,
    New,
    Len,
    Get,
    Set,
    Resize,
    Retain,
    Release,
};

/// @brief Translate a callee name into a @ref RuntimeArrayCallee enumerator.
///
/// @param callee Name referenced by the call instruction.
/// @return Matching enumerator or RuntimeArrayCallee::None when unrecognised.
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

/// @brief Validate that a runtime array helper call obeys the expected ABI.
///
/// The helper inspects the callee name, checks operand counts and types, and
/// enforces result presence/absence according to the runtime signature.  When
/// the callee is not a recognised runtime helper the function succeeds without
/// performing further validation, deferring to generic call checking instead.
///
/// @param ctx Verification context describing the instruction under review.
/// @return Successful Expected when the call conforms to the ABI; otherwise a
///         diagnostic explaining the mismatch.
Expected<void> checkRuntimeArrayCall(const VerifyCtx &ctx)
{
    const RuntimeArrayCallee calleeKind = classifyRuntimeArrayCallee(ctx.instr.callee);
    if (calleeKind == RuntimeArrayCallee::None)
        return {};

    const auto requireArgCount = [&](size_t expected) -> Expected<void> {
        if (ctx.instr.operands.size() == expected)
            return {};

        std::ostringstream ss;
        ss << "expected " << expected << " argument";
        if (expected != 1)
            ss << 's';
        ss << " to @" << ctx.instr.callee;
        return fail(ctx, ss.str());
    };

    const auto requireOperandType = [&](size_t index, Type::Kind expected, std::string_view role) {
        bool missing = false;
        const Type actual = ctx.types.valueType(ctx.instr.operands[index], &missing);
        if (missing)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << ' ' << role << " operand has unknown type";
            return fail(ctx, ss.str());
        }
        if (actual.kind != expected)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << ' ' << role << " operand must be " << kindToString(expected);
            return fail(ctx, ss.str());
        }
        return Expected<void>{};
    };

    const auto requireResultType = [&](Type::Kind expected) -> Expected<void> {
        if (!ctx.instr.result)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " must produce " << kindToString(expected) << " result";
            return fail(ctx, ss.str());
        }
        if (ctx.instr.type.kind != expected)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " result must be " << kindToString(expected);
            return fail(ctx, ss.str());
        }
        return Expected<void>{};
    };

    const auto requireNoResult = [&]() -> Expected<void> {
        if (ctx.instr.result)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " must not produce a result";
            return fail(ctx, ss.str());
        }
        if (ctx.instr.type.kind != Type::Kind::Void)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " result type must be void";
            return fail(ctx, ss.str());
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

} // namespace

/// @brief Verify operand and result types for a generic call instruction.
///
/// Runtime helper calls are validated separately by
/// @ref checkRuntimeArrayCall.  For remaining calls the verifier resolves the
/// callee to either an extern declaration or function definition, checks the
/// operand count, compares operand types, and records the result type when the
/// instruction produces a value.
///
/// @param ctx Verification context for the call instruction.
/// @return Empty Expected on success or a diagnostic on mismatch.
Expected<void> checkCall(const VerifyCtx &ctx)
{
    if (auto result = checkRuntimeArrayCall(ctx); !result)
        return result;

    const Extern *externSig = nullptr;
    const Function *fnSig = nullptr;

    if (auto it = ctx.externs.find(ctx.instr.callee); it != ctx.externs.end())
        externSig = it->second;
    else if (auto itFn = ctx.functions.find(ctx.instr.callee); itFn != ctx.functions.end())
        fnSig = itFn->second;

    if (!externSig && !fnSig)
        return fail(ctx, std::string("unknown callee @") + ctx.instr.callee);

    const size_t paramCount = externSig ? externSig->params.size() : fnSig->params.size();
    if (ctx.instr.operands.size() != paramCount)
        return fail(ctx, "call arg count mismatch");

    for (size_t i = 0; i < paramCount; ++i)
    {
        const Type expected = externSig ? externSig->params[i] : fnSig->params[i].type;
        if (ctx.types.valueType(ctx.instr.operands[i]).kind != expected.kind)
            return fail(ctx, "call arg type mismatch");
    }

    if (ctx.instr.result)
    {
        const Type ret = externSig ? externSig->retType : fnSig->retType;
        ctx.types.recordResult(ctx.instr, ret);
    }

    return {};
}

/// @brief Validate the `trap.kind` helper which materialises a runtime trap
///        enumerator.
///
/// Ensures no operands are supplied and records the i64 result type mandated by
/// the helper.
///
/// @param ctx Verification context describing the instruction.
Expected<void> checkTrapKind(const VerifyCtx &ctx)
{
    if (!ctx.instr.operands.empty())
        return fail(ctx, "trap.kind takes no operands");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
    return {};
}

/// @brief Validate the `trap.err` helper that constructs an error payload.
///
/// The helper enforces two operands (an i32 error code and str message) and
/// records an error-typed result.
///
/// @param ctx Verification context describing the instruction.
Expected<void> checkTrapErr(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() != 2)
        return fail(ctx, "trap.err expects code and text operands");

    const auto codeType = ctx.types.valueType(ctx.instr.operands[0]).kind;
    if (codeType != Type::Kind::I32)
        return fail(ctx, "trap.err code must be i32");

    const auto textType = ctx.types.valueType(ctx.instr.operands[1]).kind;
    if (textType != Type::Kind::Str)
        return fail(ctx, "trap.err text must be str");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Error));
    return {};
}

/// @brief Validate conversion from legacy BASIC error codes to trap values.
///
/// Accepts either a temporary or constant integer operand, ensuring it fits
/// within the 32-bit range expected by the runtime.  The instruction must be
/// typed as i32 so downstream passes know the resulting trap code width.
///
/// @param ctx Verification context describing the instruction.
Expected<void> checkTrapFromErr(const VerifyCtx &ctx)
{
    if (ctx.instr.operands.size() != 1)
        return fail(ctx, "invalid operand count");

    if (ctx.instr.type.kind != Type::Kind::I32)
        return fail(ctx, "trap.from_err expects i32 type");

    const auto &operand = ctx.instr.operands.front();
    if (operand.kind == il::core::Value::Kind::Temp)
    {
        if (ctx.types.valueType(operand).kind != Type::Kind::I32)
            return fail(ctx, "trap.from_err operand must be i32");
    }
    else if (operand.kind == il::core::Value::Kind::ConstInt)
    {
        if (operand.i64 < std::numeric_limits<int32_t>::min() || operand.i64 > std::numeric_limits<int32_t>::max())
            return fail(ctx, "trap.from_err constant out of range");
    }
    else
    {
        return fail(ctx, "trap.from_err operand must be i32");
    }

    return {};
}

} // namespace il::verify::checker
