//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "il/runtime/RuntimeSignatures.hpp"
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
    // i32 arrays
    NewI32,
    LenI32,
    GetI32,
    SetI32,
    ResizeI32,
    RetainI32,
    ReleaseI32,
    // string arrays
    NewStr,
    LenStr,
    GetStr,
    SetStr,
    ReleaseStr,
    // object arrays (void* elements)
    NewObj,
    LenObj,
    GetObj,
    PutObj,
    ResizeObj,
    ReleaseObj,
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
    // i32 array helpers
    if (callee == "rt_arr_i32_new")
        return RuntimeArrayCallee::NewI32;
    if (callee == "rt_arr_i32_len")
        return RuntimeArrayCallee::LenI32;
    if (callee == "rt_arr_i32_get")
        return RuntimeArrayCallee::GetI32;
    if (callee == "rt_arr_i32_set")
        return RuntimeArrayCallee::SetI32;
    if (callee == "rt_arr_i32_resize")
        return RuntimeArrayCallee::ResizeI32;
    if (callee == "rt_arr_i32_retain")
        return RuntimeArrayCallee::RetainI32;
    if (callee == "rt_arr_i32_release")
        return RuntimeArrayCallee::ReleaseI32;

    // string array helpers
    if (callee == "rt_arr_str_alloc")
        return RuntimeArrayCallee::NewStr;
    if (callee == "rt_arr_str_len")
        return RuntimeArrayCallee::LenStr;
    if (callee == "rt_arr_str_get")
        return RuntimeArrayCallee::GetStr;
    if (callee == "rt_arr_str_put")
        return RuntimeArrayCallee::SetStr;
    if (callee == "rt_arr_str_release")
        return RuntimeArrayCallee::ReleaseStr;
    // object array helpers
    if (callee == "rt_arr_obj_new")
        return RuntimeArrayCallee::NewObj;
    if (callee == "rt_arr_obj_len")
        return RuntimeArrayCallee::LenObj;
    if (callee == "rt_arr_obj_get")
        return RuntimeArrayCallee::GetObj;
    if (callee == "rt_arr_obj_put")
        return RuntimeArrayCallee::PutObj;
    if (callee == "rt_arr_obj_resize")
        return RuntimeArrayCallee::ResizeObj;
    if (callee == "rt_arr_obj_release")
        return RuntimeArrayCallee::ReleaseObj;
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
        case RuntimeArrayCallee::NewI32:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::LenI32:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::GetI32:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::SetI32:
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
        case RuntimeArrayCallee::ResizeI32:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::RetainI32:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::ReleaseI32:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireNoResult();
        }
        // String array helpers with string-typed results/operands as needed
        case RuntimeArrayCallee::NewStr:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::LenStr:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::GetStr:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            return requireResultType(Type::Kind::Str);
        }
        case RuntimeArrayCallee::SetStr:
        {
            if (auto result = requireArgCount(3); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            // ABI: third param is a pointer to a string slot (ptr), not `str`.
            if (auto result = requireOperandType(2, Type::Kind::Ptr, "value.ptr"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::ReleaseStr:
        {
            // String array release takes (ptr handle, i64 length) so runtime can
            // release contained elements before freeing the array payload.
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "length"); !result)
                return result;
            return requireNoResult();
        }
        // object arrays mirror i32 shapes, but get/put use ptr for value
        case RuntimeArrayCallee::NewObj:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::LenObj:
        {
            if (auto result = requireArgCount(1); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            return requireResultType(Type::Kind::I64);
        }
        case RuntimeArrayCallee::GetObj:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::PutObj:
        {
            if (auto result = requireArgCount(3); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "index"); !result)
                return result;
            if (auto result = requireOperandType(2, Type::Kind::Ptr, "value"); !result)
                return result;
            return requireNoResult();
        }
        case RuntimeArrayCallee::ResizeObj:
        {
            if (auto result = requireArgCount(2); !result)
                return result;
            if (auto result = requireOperandType(0, Type::Kind::Ptr, "handle"); !result)
                return result;
            if (auto result = requireOperandType(1, Type::Kind::I64, "length"); !result)
                return result;
            return requireResultType(Type::Kind::Ptr);
        }
        case RuntimeArrayCallee::ReleaseObj:
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
///          validates return type compatibility, and records the result type
///          when present.  For runtime helpers, cross-checks against the
///          runtime signature registry to catch mismatches early.  If the
///          callee is unknown or operands disagree with the signature, a
///          diagnostic is produced.
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

    // If no extern or function declaration, check if it's a known runtime helper.
    // This catches calls to runtime helpers that weren't declared as externs.
    const il::runtime::RuntimeSignature *runtimeSig = nullptr;
    if (!externSig && !fnSig)
    {
        runtimeSig = il::runtime::findRuntimeSignature(calleeName);
        if (!runtimeSig)
        {
            // For vararg helpers that may not be in the main registry, allow them
            // if they follow the rt_ naming convention (they're handled specially).
            if (!il::runtime::isVarArgCallee(calleeName))
                return fail(ctx, std::string("unknown callee @") + calleeName);
            // Vararg helpers bypass static signature validation.
            return {};
        }
    }

    // Determine parameter count and validate argument count.
    size_t paramCount = 0;
    if (externSig)
        paramCount = externSig->params.size();
    else if (fnSig)
        paramCount = fnSig->params.size();
    else if (runtimeSig)
        paramCount = runtimeSig->paramTypes.size();

    const size_t providedArgs =
        (ctx.instr.operands.size() >= argStart) ? (ctx.instr.operands.size() - argStart) : 0;
    if (providedArgs != paramCount)
    {
        std::ostringstream ss;
        ss << "call arg count mismatch: @" << calleeName << " expects " << paramCount
           << " argument" << (paramCount == 1 ? "" : "s") << " but got " << providedArgs;
        return fail(ctx, ss.str());
    }

    // Validate argument types.
    for (size_t i = 0; i < paramCount; ++i)
    {
        Type expected;
        if (externSig)
            expected = externSig->params[i];
        else if (fnSig)
            expected = fnSig->params[i].type;
        else if (runtimeSig)
            expected = runtimeSig->paramTypes[i];

        const auto actualKind = ctx.types.valueType(ctx.instr.operands[argStart + i]).kind;
        // Accept IL 'str' where runtime ABI expects 'ptr' (string handle compatibility).
        const bool strAsPtr = (expected.kind == Type::Kind::Ptr && actualKind == Type::Kind::Str);
        // Accept IL 'ptr' where runtime ABI expects 'str' (for some legacy patterns).
        const bool ptrAsStr = (expected.kind == Type::Kind::Str && actualKind == Type::Kind::Ptr);
        if (actualKind != expected.kind && !strAsPtr && !ptrAsStr)
        {
            std::ostringstream ss;
            ss << "call arg type mismatch: @" << calleeName << " parameter " << i << " expects "
               << kindToString(expected.kind) << " but got " << kindToString(actualKind);
            return fail(ctx, ss.str());
        }
    }

    // Determine expected return type.
    Type retType;
    if (externSig)
        retType = externSig->retType;
    else if (fnSig)
        retType = fnSig->retType;
    else if (runtimeSig)
        retType = runtimeSig->retType;

    // Validate return type consistency.
    if (retType.kind == Type::Kind::Void)
    {
        // Void-returning call should not have a result.
        if (ctx.instr.result)
        {
            std::ostringstream ss;
            ss << "call to void @" << calleeName << " must not have a result";
            return fail(ctx, ss.str());
        }
    }
    else
    {
        // Non-void call should have a result.
        if (!ctx.instr.result)
        {
            // Allow discarding results silently - this is common for side-effecting calls.
            // The return value is simply not used.
        }
        else
        {
            // Validate that the declared instruction type matches the return type.
            if (ctx.instr.type.kind != Type::Kind::Void && ctx.instr.type.kind != retType.kind)
            {
                // Allow str/ptr interchangeability for return types too.
                const bool strPtrCompat =
                    (ctx.instr.type.kind == Type::Kind::Str && retType.kind == Type::Kind::Ptr) ||
                    (ctx.instr.type.kind == Type::Kind::Ptr && retType.kind == Type::Kind::Str);
                if (!strPtrCompat)
                {
                    std::ostringstream ss;
                    ss << "call return type mismatch: @" << calleeName << " returns "
                       << kindToString(retType.kind) << " but instruction declares "
                       << kindToString(ctx.instr.type.kind);
                    return fail(ctx, ss.str());
                }
            }
            // Record the result type for type inference.
            ctx.types.recordResult(ctx.instr, retType);
        }
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
