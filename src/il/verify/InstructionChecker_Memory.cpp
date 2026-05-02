//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the verifier helpers that validate memory-related instructions
// such as alloca, load, store, and constant pointer forms.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Memory instruction verification helpers.
/// @details Provides functions that inspect @ref VerifyCtx and ensure memory
///          operations obey type and range rules, emitting diagnostics when they
///          do not.

#include "il/verify/InstructionCheckerShared.hpp"

#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/TypeInference.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace il::verify::checker {

using il::core::Type;
using il::core::Value;
using il::support::Diag;
using il::support::Expected;
using il::support::Severity;

namespace {

/// @brief Emit a non-fatal diagnostic associated with the current instruction.
/// @param ctx Verification context providing diagnostic sink and location.
/// @param message Warning text to append to the diagnostic.
void emitWarning(const VerifyCtx &ctx, std::string_view message) {
    ctx.diags.report(Diag{Severity::Warning, formatDiag(ctx, message), ctx.instr.loc, {}});
}

std::optional<long long> constInt(const Value &value) {
    if (value.kind != Value::Kind::ConstInt)
        return std::nullopt;
    return value.i64;
}

bool addOverflows(long long lhs, long long rhs) {
    return (rhs > 0 && lhs > std::numeric_limits<long long>::max() - rhs) ||
           (rhs < 0 && lhs < std::numeric_limits<long long>::min() - rhs);
}

std::optional<long long> typeSizeBytes(Type::Kind kind) {
    switch (kind) {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::ResumeTok:
            return 8;
        case Type::Kind::Error:
            return 24;
        case Type::Kind::Void:
            return std::nullopt;
    }
    return std::nullopt;
}

const il::core::Instr *findDef(const il::core::Function &fn, unsigned temp) {
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.result && *instr.result == temp)
                return &instr;
    return nullptr;
}

struct StackBounds {
    long long size = 0;
    long long offset = 0;
};

std::optional<StackBounds> stackBoundsForPointer(const il::core::Function &fn,
                                                 const Value &ptr,
                                                 unsigned depth = 0) {
    if (depth > 32 || ptr.kind != Value::Kind::Temp)
        return std::nullopt;
    const il::core::Instr *def = findDef(fn, ptr.id);
    if (!def)
        return std::nullopt;

    if (def->op == il::core::Opcode::Alloca && !def->operands.empty()) {
        auto size = constInt(def->operands.front());
        if (!size || *size < 0)
            return std::nullopt;
        return StackBounds{*size, 0};
    }

    if (def->op != il::core::Opcode::GEP || def->operands.size() < 2)
        return std::nullopt;
    auto base = stackBoundsForPointer(fn, def->operands[0], depth + 1);
    auto offset = constInt(def->operands[1]);
    if (!base || !offset)
        return std::nullopt;
    if (addOverflows(base->offset, *offset))
        return std::nullopt;
    base->offset += *offset;
    return base;
}

Expected<void> checkStackAccessBounds(const VerifyCtx &ctx,
                                      const Value &ptr,
                                      Type::Kind accessKind,
                                      std::string_view operation) {
    const auto size = typeSizeBytes(accessKind);
    if (!size)
        return fail(ctx, std::string(operation) + " type has no storage size");

    auto bounds = stackBoundsForPointer(ctx.fn, ptr);
    if (!bounds)
        return {};

    if (bounds->offset < 0 || bounds->offset > bounds->size)
        return fail(ctx, std::string(operation) + " outside alloca");
    if (*size > bounds->size - bounds->offset)
        return fail(ctx, std::string(operation) + " exceeds alloca bounds");
    return {};
}

} // namespace

/// @brief Verify the semantics of the @c alloca instruction.
/// @details Ensures the size operand exists, is i64-typed, and warns when the
///          requested allocation is suspiciously large.  Records the result as a
///          pointer type when validation succeeds.
Expected<void> checkAlloca(const VerifyCtx &ctx) {
    if (ctx.instr.operands.empty())
        return fail(ctx, "missing size operand");

    if (ctx.types.valueType(ctx.instr.operands[0]).kind != Type::Kind::I64)
        return fail(ctx, "size must be i64");

    if (ctx.instr.operands[0].kind == Value::Kind::ConstInt) {
        const long long size = ctx.instr.operands[0].i64;
        if (size < 0)
            return fail(ctx, "negative alloca size");
        if (size > (1LL << 20))
            emitWarning(ctx, "huge alloca");
    }

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify the @c gep instruction.
/// @details Checks operand count and records the result as a pointer type.  More
///          advanced structural checks are deferred to future passes.
Expected<void> checkGEP(const VerifyCtx &ctx) {
    if (ctx.instr.operands.size() < 2)
        return fail(ctx, "invalid operand count");

    bool baseMissing = false;
    if (ctx.types.valueType(ctx.instr.operands[0], &baseMissing).kind != Type::Kind::Ptr) {
        if (baseMissing)
            return fail(ctx, "base pointer type is unknown");
        return fail(ctx, "base pointer must be ptr");
    }

    bool offsetMissing = false;
    if (ctx.types.valueType(ctx.instr.operands[1], &offsetMissing).kind != Type::Kind::I64) {
        if (offsetMissing)
            return fail(ctx, "offset type is unknown");
        return fail(ctx, "offset must be i64");
    }

    if (const auto offset = constInt(ctx.instr.operands[1])) {
        if (auto bounds = stackBoundsForPointer(ctx.fn, ctx.instr.operands[0])) {
            if (addOverflows(bounds->offset, *offset))
                return fail(ctx, "gep offset overflow");
            const long long absolute = bounds->offset + *offset;
            if (absolute < 0 || absolute >= bounds->size)
                return fail(ctx, "gep offset outside alloca");
        }
    }

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify the @c load instruction.
/// @details Requires a pointer operand and records the result as the annotated
///          instruction type, reporting diagnostics when the pointer type does
///          not match expectations.
Expected<void> checkLoad(const VerifyCtx &ctx) {
    if (ctx.instr.operands.empty())
        return fail(ctx, "missing operand");

    if (ctx.types.valueType(ctx.instr.operands[0]).kind != Type::Kind::Ptr)
        return fail(ctx, "pointer type mismatch");

    if (auto result =
            checkStackAccessBounds(ctx, ctx.instr.operands[0], ctx.instr.type.kind, "load");
        !result)
        return result;

    ctx.types.recordResult(ctx.instr, ctx.instr.type);
    return {};
}

/// @brief Verify the @c store instruction.
/// @details Validates pointer operand type, checks boolean stores for legal
///          literal values, and enforces integer literal bounds based on the
///          target type.
Expected<void> checkStore(const VerifyCtx &ctx) {
    if (ctx.instr.operands.size() < 2)
        return fail(ctx, "invalid operand count");

    bool pointerMissing = false;
    const Type pointerType = ctx.types.valueType(ctx.instr.operands[0], &pointerMissing);
    if (pointerMissing)
        return fail(ctx, "pointer operand type is unknown");

    if (pointerType.kind != Type::Kind::Ptr)
        return fail(ctx, "pointer type mismatch");

    if (auto result =
            checkStackAccessBounds(ctx, ctx.instr.operands[0], ctx.instr.type.kind, "store");
        !result)
        return result;

    const bool isBoolConst = ctx.instr.type.kind == Type::Kind::I1 &&
                             ctx.instr.operands[1].kind == Value::Kind::ConstInt;
    if (isBoolConst) {
        const long long value = ctx.instr.operands[1].i64;
        if (value != 0 && value != 1)
            return fail(ctx, "boolean store expects 0 or 1");
    } else if (ctx.instr.operands[1].kind == Value::Kind::ConstInt &&
               (ctx.instr.type.kind == Type::Kind::I16 || ctx.instr.type.kind == Type::Kind::I32)) {
        const long long value = ctx.instr.operands[1].i64;
        const long long min = ctx.instr.type.kind == Type::Kind::I16
                                  ? std::numeric_limits<int16_t>::min()
                                  : std::numeric_limits<int32_t>::min();
        const long long max = ctx.instr.type.kind == Type::Kind::I16
                                  ? std::numeric_limits<int16_t>::max()
                                  : std::numeric_limits<int32_t>::max();
        if (value < min || value > max)
            return fail(ctx, "value out of range for store type");
    }

    return {};
}

/// @brief Verify the @c addr.of instruction.
/// @details Requires a single global-address operand and records the result as a
///          pointer type.
Expected<void> checkAddrOf(const VerifyCtx &ctx) {
    if (ctx.instr.operands.size() != 1 || ctx.instr.operands[0].kind != Value::Kind::GlobalAddr)
        return fail(ctx, "operand must be global");

    const std::string &name = ctx.instr.operands[0].str;
    auto it = ctx.globals.find(name);
    if (it == ctx.globals.end())
        return fail(ctx, "unknown global @" + name);
    if (it->second->type.kind != Type::Kind::Str)
        return fail(ctx, "addr.of operand must name a string global");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify the @c const.str instruction.
/// @details Confirms the operand references a known string global and records
///          the result as a string type.
Expected<void> checkConstStr(const VerifyCtx &ctx) {
    if (ctx.instr.operands.size() != 1 || ctx.instr.operands[0].kind != Value::Kind::GlobalAddr)
        return fail(ctx, "unknown string global");

    const std::string &name = ctx.instr.operands[0].str;
    auto it = ctx.globals.find(name);
    if (it == ctx.globals.end())
        return fail(ctx, "unknown string global @" + name);
    if (it->second->type.kind != Type::Kind::Str)
        return fail(ctx, "const.str operand must name a string global");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Str));
    return {};
}

/// @brief Verify the @c gaddr instruction.
/// @details Requires a declared non-string global with addressable storage.
Expected<void> checkGAddr(const VerifyCtx &ctx) {
    if (ctx.instr.operands.size() != 1 || ctx.instr.operands[0].kind != Value::Kind::GlobalAddr)
        return fail(ctx, "gaddr operand must be global");

    const std::string &name = ctx.instr.operands[0].str;
    auto it = ctx.globals.find(name);
    if (it == ctx.globals.end())
        return fail(ctx, "unknown global @" + name);

    const Type::Kind kind = it->second->type.kind;
    if (kind == Type::Kind::Str)
        return fail(ctx, "gaddr requires a scalar storage global, not a string constant");
    if (kind == Type::Kind::Void || kind == Type::Kind::Error || kind == Type::Kind::ResumeTok)
        return fail(ctx, "gaddr requires a scalar storage global");

    ctx.types.recordResult(ctx.instr, Type(Type::Kind::Ptr));
    return {};
}

/// @brief Verify the @c const.null instruction.
/// @details Accepts only pointer-like result annotations and records the result
///          for downstream passes.
Expected<void> checkConstNull(const VerifyCtx &ctx) {
    Type resultType = ctx.instr.type;
    switch (resultType.kind) {
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            break;
        default:
            return fail(ctx, "const.null result type must be ptr, str, error, or resumetok");
    }

    ctx.types.recordResult(ctx.instr, resultType);
    return {};
}

} // namespace il::verify::checker
