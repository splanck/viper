// File: src/il/verify/InstructionChecker_Runtime.cpp
// Purpose: Implements runtime and call-related instruction verification helpers.
// Key invariants: Ensures extern signatures and runtime helper usage obey operand/result typing rules.
// Ownership/Lifetime: Operates on VerifyCtx without capturing external ownership.
// Links: docs/il-guide.md#reference

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

    const Type retType = externSig ? externSig->retType : fnSig->retType;

    if (retType.kind == Type::Kind::Void)
    {
        if (ctx.instr.result)
            return fail(ctx, "void callee must not produce a result");
        if (ctx.instr.type.kind != Type::Kind::Void)
            return fail(ctx, "void callee must use void type");
        return {};
    }

    if (!ctx.instr.result)
        return fail(ctx, "non-void callee requires a result");

    if (ctx.instr.type.kind != retType.kind)
        return fail(ctx, "call result type mismatch");

    ctx.types.recordResult(ctx.instr, retType);

    return {};
}

Expected<void> checkTrapKind(const VerifyCtx &ctx)
{
    if (!ctx.instr.operands.empty())
        return fail(ctx, "trap.kind takes no operands");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::I64));
    return {};
}

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
