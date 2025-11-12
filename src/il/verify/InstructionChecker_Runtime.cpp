//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements verification helpers for runtime calls and trap-related
// instructions.  The routines validate argument counts, operand types, and
// result expectations against both opcode metadata and runtime signature rules
// to ensure modules adhere to the IL specification before execution.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime-specific instruction verification utilities.
/// @details Provides helpers that validate runtime array operations, indirect
///          calls, and trap instructions.  Diagnostics are routed through the
///          shared @ref VerifyCtx infrastructure so all failures include
///          location and opcode context.

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
using il::core::kindToString;
using il::core::Type;
using il::support::Expected;

namespace
{

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

/// @brief Map a runtime helper name to its array-handling category.
/// @details The runtime exposes a fixed set of array helpers with predictable
///          names.  This function compares @p callee against the supported
///          strings and returns the corresponding enumerator so subsequent
///          verification can apply helper-specific rules.  Unknown names fall
///          back to @ref RuntimeArrayCallee::None so other verifiers may handle
///          the call.
/// @param callee Runtime helper name extracted from the instruction.
/// @return Enumerated helper classification.
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

/// @brief Validate runtime array helper invocations.
/// @details Ensures that helper-specific operand counts, operand types, and
///          result expectations are satisfied for array allocation, indexing,
///          mutation, and reference-count management helpers.  Diagnostics are
///          produced through @ref fail when the call deviates from the contract;
///          otherwise the function returns success without modifying state.
/// @param ctx Verification context describing the current instruction.
/// @return Empty result on success; structured diagnostic on error.
Expected<void> checkRuntimeArrayCall(const VerifyCtx &ctx)
{
    const RuntimeArrayCallee calleeKind = classifyRuntimeArrayCallee(ctx.instr.callee);
    if (calleeKind == RuntimeArrayCallee::None)
        return {};

    // Helper that enforces an exact operand count and emits a descriptive error
    // when the instruction does not provide the required number of arguments.
    const auto requireArgCount = [&](size_t expected) -> Expected<void>
    {
        if (ctx.instr.operands.size() == expected)
            return {};

        std::ostringstream ss;
        ss << "expected " << expected << " argument";
        if (expected != 1)
            ss << 's';
        ss << " to @" << ctx.instr.callee;
        return fail(ctx, ss.str());
    };

    // Helper used to check operand types against the expected runtime
    // signature, emitting contextual diagnostics when values are missing or of
    // the wrong type.
    const auto requireOperandType = [&](size_t index, Type::Kind expected, std::string_view role)
    {
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
            ss << "@" << ctx.instr.callee << ' ' << role << " operand must be "
               << kindToString(expected);
            return fail(ctx, ss.str());
        }
        return Expected<void>{};
    };

    // Helper that verifies the presence and type of the instruction result for
    // helpers expected to return a value.
    const auto requireResultType = [&](Type::Kind expected) -> Expected<void>
    {
        if (!ctx.instr.result)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " must produce " << kindToString(expected)
               << " result";
            return fail(ctx, ss.str());
        }
        // Record the result type so IL parsed from text gets proper type inference.
        ctx.types.recordResult(ctx.instr, Type(expected));
        if (ctx.instr.type.kind != Type::Kind::Void && ctx.instr.type.kind != expected)
        {
            std::ostringstream ss;
            ss << "@" << ctx.instr.callee << " result must be " << kindToString(expected);
            return fail(ctx, ss.str());
        }
        return Expected<void>{};
    };

    // Helper ensuring helpers that should not produce a value remain
    // side-effect only.
    const auto requireNoResult = [&]() -> Expected<void>
    {
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

/// @brief Verify indirect calls to functions and externs.
/// @details Resolves the callee against known functions and externs, checks
///          argument counts and operand types against the resolved signature,
///          and records the result type when present.  If the callee is unknown
///          or operands disagree with the signature, a diagnostic is produced.
/// @param ctx Verification context containing call operands and signature maps.
/// @return Success when the call matches the signature; otherwise an error.
Expected<void> checkCall(const VerifyCtx &ctx)
{
    // Handle direct calls with special runtime array helpers; skip for indirect.
    if (ctx.instr.op == il::core::Opcode::Call)
    {
        if (auto result = checkRuntimeArrayCall(ctx); !result)
            return result;
    }

    // Resolve callee name and argument slice depending on opcode kind.
    std::string calleeName;
    size_t argStart = 0;
    if (ctx.instr.op == il::core::Opcode::Call)
    {
        calleeName = ctx.instr.callee;
        argStart = 0;
    }
    else if (ctx.instr.op == il::core::Opcode::CallIndirect)
    {
        if (ctx.instr.operands.empty())
            return fail(ctx, "call.indirect missing callee operand");
        const auto &calleeVal = ctx.instr.operands[0];
        if (calleeVal.kind == il::core::Value::Kind::GlobalAddr)
        {
            calleeName = calleeVal.str;
            argStart = 1;
        }
        else
        {
            // Pointer-based indirect call (e.g., interface dispatch). Skip static signature checks.
            return {};
        }
    }
    else
    {
        // Not a call; defer to default checker.
        return {};
    }

    const Extern *externSig = nullptr;
    const Function *fnSig = nullptr;

    if (auto it = ctx.externs.find(calleeName); it != ctx.externs.end())
        externSig = it->second;
    else if (auto itFn = ctx.functions.find(calleeName); itFn != ctx.functions.end())
        fnSig = itFn->second;

    if (!externSig && !fnSig)
        return fail(ctx, std::string("unknown callee @") + calleeName);

    const size_t paramCount = externSig ? externSig->params.size() : fnSig->params.size();
    const size_t providedArgs =
        (ctx.instr.operands.size() >= argStart) ? (ctx.instr.operands.size() - argStart) : 0;
    if (providedArgs != paramCount)
        return fail(ctx, "call arg count mismatch");

    for (size_t i = 0; i < paramCount; ++i)
    {
        const Type expected = externSig ? externSig->params[i] : fnSig->params[i].type;
        if (ctx.types.valueType(ctx.instr.operands[argStart + i]).kind != expected.kind)
            return fail(ctx, "call arg type mismatch");
    }

    if (ctx.instr.result)
    {
        const Type ret = externSig ? externSig->retType : fnSig->retType;
        ctx.types.recordResult(ctx.instr, ret);
    }

    return {};
}

/// @brief Verify the @c trap.kind intrinsic.
/// @details Ensures the instruction takes no operands and records the result
///          type as @c i64 so subsequent instructions can reason about the
///          produced trap code.
/// @param ctx Verification context for the @c trap.kind instruction.
/// @return Success when the instruction shape is valid; otherwise an error.
Expected<void> checkTrapKind(const VerifyCtx &ctx)
{
    if (!ctx.instr.operands.empty())
        return fail(ctx, "trap.kind takes no operands");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
    return {};
}

/// @brief Verify the @c trap.err intrinsic.
/// @details Checks that two operands are provided, validates their inferred
///          types (@c i32 error code and @c str message), and records the
///          resulting @c error type in the type lattice.
/// @param ctx Verification context for the @c trap.err instruction.
/// @return Success when operand counts and types are correct.
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

/// @brief Verify the @c trap.from_err intrinsic.
/// @details Ensures a single @c i32 operand is provided either as a temporary or
///          as an in-range integer constant, and checks that the instruction's
///          declared type is also @c i32.  This guards the runtime error bridge
///          against invalid conversions.
/// @param ctx Verification context for the @c trap.from_err instruction.
/// @return Success when operand and result types satisfy the intrinsic
///         contract.
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
        if (operand.i64 < std::numeric_limits<int32_t>::min() ||
            operand.i64 > std::numeric_limits<int32_t>::max())
            return fail(ctx, "trap.from_err constant out of range");
    }
    else
    {
        return fail(ctx, "trap.from_err operand must be i32");
    }

    return {};
}

} // namespace il::verify::checker
