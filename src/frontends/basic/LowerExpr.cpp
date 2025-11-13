//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/LowerExpr.cpp
// Purpose: Implement expression lowering helpers for the BASIC front end.
// Key invariants: Expression lowering preserves operand types, injecting
//                 conversions to match IL expectations and runtime helpers.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md, docs/basic-language.md#expressions
//
//===----------------------------------------------------------------------===//

// Requires the consolidated Lowerer interface for expression lowering helpers.
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/LowerExprBuiltin.hpp"
#include "frontends/basic/LowerExprLogical.hpp"
#include "frontends/basic/LowerExprNumeric.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "viper/il/Module.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower a BASIC variable reference into an IL value.
/// @param v Variable expression that names the slot to read from.
/// @return Materialized value and type pair for the requested variable.
/// @details
/// - Control flow: Executes entirely within the current basic block without
///   branching or block creation.
/// - Emitted IL: Issues a load from the stack slot recorded in the symbol
///   metadata, selecting pointer, string, floating, or boolean types as
///   required.
/// - Side effects: Updates @ref curLoc so diagnostics and subsequent
///   instructions are tagged with @p v's source location.
Lowerer::RVal Lowerer::lowerVarExpr(const VarExpr &v)
{
    LocationScope loc(*this, v.loc);
    auto storage = resolveVariableStorage(v.name, v.loc);
    assert(storage && "variable should have resolved storage");
    Type ty = storage->slotInfo.type;
    Value val = emitLoad(ty, storage->pointer);
    return {val, ty};
}

/// @brief Lower a `UBOUND` query into an IL call and subtraction.
///
/// @details Loads the array slot, calls into the runtime to obtain the logical
///          length, and subtracts one to produce the highest valid index.  The
///          helper assumes semantic analysis already verified the symbol is an
///          array.
///
/// @param expr Array upper-bound expression.
/// @return Pair containing the computed upper bound and its integer type.
Lowerer::RVal Lowerer::lowerUBoundExpr(const UBoundExpr &expr)
{
    LocationScope loc(*this, expr.loc);
    const auto *sym = findSymbol(expr.name);
    assert(sym && sym->slotId && "UBOUND requires materialized array slot");
    Value slot = Value::temp(*sym->slotId);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);

    // Use appropriate length function based on array element type
    Value len;
    if (sym->type == AstType::Str)
        len = emitCallRet(Type(Type::Kind::I64), "rt_arr_str_len", {base});
    else
        len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});

    Value upper = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), len, Value::constInt(1));
    return {upper, Type(Type::Kind::I64)};
}

/// @brief Materialize a boolean result using custom then/else emitters.
/// @param cond Lowered condition controlling the branch.
/// @param loc Source location associated with the boolean expression.
/// @param emitThen Callback that stores the "true" value into the supplied slot.
/// @param emitElse Callback that stores the "false" value into the supplied slot.
/// @param thenLabelBase Optional label stem used when naming the then block.
/// @param elseLabelBase Optional label stem used when naming the else block.
/// @param joinLabelBase Optional label stem used when naming the join block.
/// @return Boolean result paired with its IL `i1` type.
/// @details
/// - Control flow: Saves the originating block, requests a structured branch
///   from @ref emitBoolFromBranches, and then wires up the conditional branch
///   from @p cond back at the origin before resuming in the join block.
/// - Emitted IL: Allocates a temporary boolean slot, lets @p emitThen and
///   @p emitElse populate it via @ref emitStore, and finally emits a
///   conditional branch via @ref emitCBr. Callers may translate the `i1`
///   result to BASIC logical words with @ref emitBasicLogicalI64.
/// - Side effects: Mutates @ref cur and @ref curLoc while stitching together
///   the control-flow graph and asserts both closures emitted their blocks.
Lowerer::RVal Lowerer::lowerBoolBranchExpr(Value cond,
                                           il::support::SourceLoc loc,
                                           const std::function<void(Value)> &emitThen,
                                           const std::function<void(Value)> &emitElse,
                                           std::string_view thenLabelBase,
                                           std::string_view elseLabelBase,
                                           std::string_view joinLabelBase)
{
    LocationScope location(*this, loc);
    ProcedureContext &ctx = context();
    BasicBlock *origin = ctx.current();
    BasicBlock *thenBlk = nullptr;
    BasicBlock *elseBlk = nullptr;

    std::string_view thenBase =
        thenLabelBase.empty() ? std::string_view("bool_then") : thenLabelBase;
    std::string_view elseBase =
        elseLabelBase.empty() ? std::string_view("bool_else") : elseLabelBase;
    std::string_view joinBase =
        joinLabelBase.empty() ? std::string_view("bool_join") : joinLabelBase;

    IlValue result = emitBoolFromBranches(
        [&](Value slot)
        {
            thenBlk = ctx.current();
            emitThen(slot);
        },
        [&](Value slot)
        {
            elseBlk = ctx.current();
            emitElse(slot);
        },
        thenBase,
        elseBase,
        joinBase);

    assert(thenBlk && elseBlk);

    BasicBlock *joinBlk = ctx.current();

    ctx.setCurrent(origin);
    emitCBr(cond, thenBlk, elseBlk);
    ctx.setCurrent(joinBlk);
    return {result, ilBoolTy()};
}

/// @brief Emit a constant 64-bit integer value via the emitter facade.
///
/// @param v Literal integer to materialise.
/// @return IL value representing the constant.
Lowerer::Value Lowerer::emitConstI64(std::int64_t v)
{
    return emitter().emitConstI64(v);
}

/// @brief Zero-extend an `i1` boolean into the BASIC logical integer form.
///
/// @param val Boolean value to extend.
/// @return Result of invoking the emitter helper.
Lowerer::Value Lowerer::emitZext1ToI64(Value val)
{
    return emitter().emitZext1ToI64(val);
}

/// @brief Emit an integer subtraction.
///
/// @param lhs Left operand.
/// @param rhs Right operand.
/// @return Difference computed by the emitter helper.
Lowerer::Value Lowerer::emitISub(Value lhs, Value rhs)
{
    return emitter().emitISub(lhs, rhs);
}

/// @brief Convert an `i1` logical value into BASIC's `-1/0` convention.
///
/// @param b1 Boolean value to normalise.
/// @return 64-bit integer representing BASIC truthiness.
Lowerer::Value Lowerer::emitBasicLogicalI64(Value b1)
{
    return emitter().emitBasicLogicalI64(b1);
}

/// @brief Lower a unary BASIC expression, currently handling logical NOT.
/// @param u Unary AST node to translate.
/// @return Boolean result produced by evaluating @p u.
/// @details
/// - Control flow: Evaluates the operand within the current block and then
///   reuses @ref lowerBoolBranchExpr to create then/else continuations that
///   store the negated boolean result.
/// - Emitted IL: Optionally truncates the operand to `i1` via
///   @ref emitUnary and emits stores of `false`/`true` constants produced by
///   @ref emitBoolConst.
/// - Side effects: Updates @ref curLoc so generated instructions are annotated
///   with the operand's location.
Lowerer::RVal Lowerer::lowerUnaryExpr(const UnaryExpr &u)
{
    switch (u.op)
    {
        case UnaryExpr::Op::LogicalNot:
        {
            LocationScope loc(*this, u.loc);
            RVal val = lowerExpr(*u.expr);
            // Classic BASIC NOT: bitwise complement (XOR with all bits set)
            // NOT 0 = -1, NOT -1 = 0, NOT n = ~n
            if (val.type.kind != Type::Kind::I64)
                val = coerceToI64(std::move(val), u.loc);
            curLoc = u.loc;
            Value allBitsSet = Value::constInt(-1);
            Value result = emitCommon(u.loc).logical_xor(val.value, allBitsSet);
            return {result, Type(Type::Kind::I64)};
        }
        case UnaryExpr::Op::Plus:
            return lowerExpr(*u.expr);
        case UnaryExpr::Op::Negate:
        {
            LocationScope loc(*this, u.loc);
            RVal value = lowerExpr(*u.expr);
            if (value.type.kind == Type::Kind::I1)
                value = coerceToI64(std::move(value), u.loc);
            if (value.type.kind == Type::Kind::F64)
            {
                Value neg = emitBinary(
                    Opcode::FSub, Type(Type::Kind::F64), Value::constFloat(0.0), value.value);
                return {neg, Type(Type::Kind::F64)};
            }
            if (value.type.kind == Type::Kind::I16 || value.type.kind == Type::Kind::I32 ||
                value.type.kind == Type::Kind::I64)
            {
                Value neg = emitCheckedNeg(value.type, value.value);
                return {neg, value.type};
            }
            value = coerceToI64(std::move(value), u.loc);
            if (value.type.kind == Type::Kind::I16 || value.type.kind == Type::Kind::I32 ||
                value.type.kind == Type::Kind::I64)
            {
                Value neg = emitCheckedNeg(value.type, value.value);
                return {neg, value.type};
            }
            return value;
        }
    }
    return lowerExpr(*u.expr);
}

/// @brief Lower BASIC logical binary expressions, including short-circuiting.
/// @param b Binary expression describing AND/OR semantics.
/// @return Boolean value that reflects @p b's evaluation.
/// @details
/// - Control flow: For short-circuit variants the routine uses
///   @ref lowerBoolBranchExpr to fork evaluation, only lowering the right-hand
///   operand in the taken branch; non-short-circuit forms evaluate both sides
///   eagerly and still funnel results through the helper to ensure a material
///   slot exists.
/// - Emitted IL: Converts operands to `i1` when required, emits stores of
///   boolean constants, and relies on @ref lowerBoolBranchExpr to emit the
///   conditional branch wiring.
/// - Side effects: Updates @ref curLoc for each emitted instruction and may
///   recursively call @ref lowerExpr on child expressions.
/// @brief Dispatch lowering for all BASIC binary expressions.
/// @param b Binary AST node to translate.
/// @return Lowered value alongside its IL type.
/// @details
/// - Control flow: Delegates to specialized helpers for logical and numeric
///   categories, letting those routines introduce any necessary branching.
/// - Emitted IL: Depends on the dispatched helper, ranging from control-flow
///   merges to arithmetic instructions and runtime calls.
/// - Side effects: May trigger recursive @ref lowerExpr invocations for both
///   operands and updates @ref curLoc through the delegated helpers.
Lowerer::RVal Lowerer::lowerBinaryExpr(const BinaryExpr &b)
{
    if (b.op == BinaryExpr::Op::LogicalAndShort || b.op == BinaryExpr::Op::LogicalOrShort ||
        b.op == BinaryExpr::Op::LogicalAnd || b.op == BinaryExpr::Op::LogicalOr)
        return lowerLogicalBinary(b);
    if (b.op == BinaryExpr::Op::IDiv || b.op == BinaryExpr::Op::Mod)
        return lowerDivOrMod(b);

    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    if (b.op == BinaryExpr::Op::Pow)
        return lowerPowBinary(b, std::move(lhs), std::move(rhs));
    // Route string operations to string-specific lowering
    if ((b.op == BinaryExpr::Op::Add || b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne ||
         b.op == BinaryExpr::Op::Lt || b.op == BinaryExpr::Op::Le ||
         b.op == BinaryExpr::Op::Gt || b.op == BinaryExpr::Op::Ge) &&
        lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        return lowerStringBinary(b, lhs, rhs);
    return lowerNumericBinary(b, lhs, rhs);
}

/*
 * NOTE: Widening I16/I32 to I64 must produce a real i64 SSA value.
 * Do not mutate the type tag alone; integer comparisons (icmp_*) require true i64 operands.
 * We sign-extend using (x << shift) >> shift to preserve negative values (e.g., EOF = -1).
 */
/// @brief Coerce a value into a 64-bit integer representation.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `i64` type when conversion occurs.
Lowerer::RVal Lowerer::coerceToI64(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(*this, loc);
    switch (v.type.kind)
    {
        case Type::Kind::I64:
            return v;

        case Type::Kind::I1:
            v.value = emitBasicLogicalI64(v.value);
            v.type = Type(Type::Kind::I64);
            return v;

        case Type::Kind::F64:
            v.value = emitUnary(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), v.value);
            v.type = Type(Type::Kind::I64);
            return v;

        case Type::Kind::I16:
        case Type::Kind::I32:
        {
            const int fromBits = (v.type.kind == Type::Kind::I32) ? 32 : 16;
            v.value = emitCommon(loc).widen_to(v.value, fromBits, 64);
            v.type = Type(Type::Kind::I64);
            return v;
        }

        default:
            return v;
    }
}

/// @brief Coerce a value into a 64-bit floating-point representation.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `f64` type when conversion occurs.
Lowerer::RVal Lowerer::coerceToF64(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(*this, loc);
    if (v.type.kind == Type::Kind::F64)
        return v;
    v = coerceToI64(std::move(v), loc);
    if (v.type.kind == Type::Kind::I64)
    {
        v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
        v.type = Type(Type::Kind::F64);
    }
    return v;
}

/// @brief Coerce a value into a boolean representation.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `i1` type when conversion occurs.
Lowerer::RVal Lowerer::coerceToBool(RVal v, il::support::SourceLoc loc)
{
    LocationScope location(*this, loc);
    if (v.type.kind == Type::Kind::I1)
        return v;
    if (v.type.kind == Type::Kind::F64 || v.type.kind == Type::Kind::I16 ||
        v.type.kind == Type::Kind::I32 || v.type.kind == Type::Kind::I64)
        v = coerceToI64(std::move(v), loc);
    if (v.type.kind != Type::Kind::I1)
    {
        v.value = emitUnary(Opcode::Trunc1, ilBoolTy(), v.value);
        v.type = ilBoolTy();
    }
    return v;
}

/// @brief Ensure a value is represented as a 64-bit integer.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `i64` type.
Lowerer::RVal Lowerer::ensureI64(RVal v, il::support::SourceLoc loc)
{
    return coerceToI64(std::move(v), loc);
}

/// @brief Ensure a value is represented as a 64-bit floating-point number.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `f64` type.
Lowerer::RVal Lowerer::ensureF64(RVal v, il::support::SourceLoc loc)
{
    return coerceToF64(std::move(v), loc);
}

/// @brief Entry point for lowering BASIC expressions to IL.
/// @param expr Expression subtree to translate.
/// @return Lowered value accompanied by its IL type.
/// @details
/// - Control flow: Performs type-directed dispatch, with individual cases
///   optionally creating additional blocks through specialized helpers.
/// - Emitted IL: Encompasses constant materialization, runtime calls, and
///   instruction emission delegated to helper routines.
/// - Side effects: Updates @ref curLoc, may mutate runtime requirement flags,
///   and recursively lowers nested expressions.
} // namespace il::frontends::basic
