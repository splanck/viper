// File: src/frontends/basic/LowerExpr.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements expression lowering helpers for the BASIC front end.
// Key invariants: Expression lowering preserves operand types, injecting
//                 conversions to match IL expectations and runtime helpers.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

// Requires the consolidated Lowerer interface for expression lowering helpers.
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <algorithm>
#include <cassert>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <limits>

using namespace il::core;

namespace il::frontends::basic
{

namespace
{
std::string_view logicalOperatorDisplayName(BinaryExpr::Op op) noexcept
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            return "ANDALSO";
        case BinaryExpr::Op::LogicalOrShort:
            return "ORELSE";
        case BinaryExpr::Op::LogicalAnd:
            return "AND";
        case BinaryExpr::Op::LogicalOr:
            return "OR";
        default:
            break;
    }
    return "<logical>";
}

constexpr std::string_view kDiagUnsupportedLogicalOperator = "B4002";
constexpr std::string_view kDiagUnsupportedCustomBuiltinVariant = "B4003";
} // namespace

/// @brief Expression visitor that lowers nodes via Lowerer helpers.
class LowererExprVisitor final : public ExprVisitor
{
  public:
    explicit LowererExprVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visit(const IntExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{Value::constInt(expr.value),
                                il::core::Type(il::core::Type::Kind::I64)};
    }

    void visit(const FloatExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{Value::constFloat(expr.value),
                                il::core::Type(il::core::Type::Kind::F64)};
    }

    void visit(const StringExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        std::string lbl = lowerer_.getStringLabel(expr.value);
        Value tmp = lowerer_.emitConstStr(lbl);
        result_ = Lowerer::RVal{tmp, il::core::Type(il::core::Type::Kind::Str)};
    }

    void visit(const BoolExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        if (lowerer_.context().current() == nullptr)
        {
            Value logical = Value::constInt(expr.value ? -1 : 0);
            result_ = Lowerer::RVal{logical, il::core::Type(il::core::Type::Kind::I64)};
            return;
        }
        Value raw = lowerer_.emitBoolConst(expr.value);
        lowerer_.curLoc = expr.loc;
        Value logical = lowerer_.emitBasicLogicalI64(raw);
        result_ = Lowerer::RVal{logical, il::core::Type(il::core::Type::Kind::I64)};
    }

    void visit(const VarExpr &expr) override { result_ = lowerer_.lowerVarExpr(expr); }

    void visit(const ArrayExpr &expr) override
    {
        Lowerer::ArrayAccess access = lowerer_.lowerArrayAccess(expr);
        lowerer_.curLoc = expr.loc;
        Value val = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                         "rt_arr_i32_get",
                                         {access.base, access.index});
        result_ = Lowerer::RVal{val, il::core::Type(il::core::Type::Kind::I64)};
    }

    void visit(const UnaryExpr &expr) override { result_ = lowerer_.lowerUnaryExpr(expr); }

    void visit(const BinaryExpr &expr) override { result_ = lowerer_.lowerBinaryExpr(expr); }

    void visit(const BuiltinCallExpr &expr) override
    {
        result_ = lowerer_.lowerBuiltinCall(expr);
    }

    void visit(const LBoundExpr &expr) override
    {
        lowerer_.curLoc = expr.loc;
        result_ = Lowerer::RVal{Value::constInt(0), il::core::Type(il::core::Type::Kind::I64)};
    }

    void visit(const UBoundExpr &expr) override
    {
        result_ = lowerer_.lowerUBoundExpr(expr);
    }

    void visit(const CallExpr &expr) override
    {
        const auto *signature = lowerer_.findProcSignature(expr.callee);
        std::vector<Value> args;
        args.reserve(expr.args.size());
        for (size_t i = 0; i < expr.args.size(); ++i)
        {
            Lowerer::RVal arg = lowerer_.lowerExpr(*expr.args[i]);
            if (signature && i < signature->paramTypes.size())
            {
                il::core::Type paramTy = signature->paramTypes[i];
                if (paramTy.kind == il::core::Type::Kind::F64)
                {
                    arg = lowerer_.coerceToF64(std::move(arg), expr.loc);
                }
                else if (paramTy.kind == il::core::Type::Kind::I64)
                {
                    arg = lowerer_.coerceToI64(std::move(arg), expr.loc);
                }
            }
            args.push_back(arg.value);
        }
        lowerer_.curLoc = expr.loc;
        if (signature && signature->retType.kind != il::core::Type::Kind::Void)
        {
            Value res = lowerer_.emitCallRet(signature->retType, expr.callee, args);
            result_ = Lowerer::RVal{res, signature->retType};
        }
        else
        {
            lowerer_.emitCall(expr.callee, args);
            result_ = Lowerer::RVal{Value::constInt(0),
                                    il::core::Type(il::core::Type::Kind::I64)};
        }
    }

    [[nodiscard]] Lowerer::RVal result() const noexcept { return result_; }

  private:
    Lowerer &lowerer_;
    Lowerer::RVal result_{Value::constInt(0),
                          il::core::Type(il::core::Type::Kind::I64)};
};

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
    curLoc = v.loc;
    const auto *sym = findSymbol(v.name);
    assert(sym && sym->slotId);
    Value ptr = Value::temp(*sym->slotId);
    SlotType slotInfo = getSlotType(v.name);
    Type ty = slotInfo.type;
    Value val = emitLoad(ty, ptr);
    return {val, ty};
}

Lowerer::RVal Lowerer::lowerUBoundExpr(const UBoundExpr &expr)
{
    curLoc = expr.loc;
    const auto *sym = findSymbol(expr.name);
    assert(sym && sym->slotId && "UBOUND requires materialized array slot");
    Value slot = Value::temp(*sym->slotId);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    curLoc = expr.loc;
    Value len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
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
/// @return Boolean result paired with its IL type.
/// @details
/// - Control flow: Saves the originating block, requests a structured branch
///   from @ref emitBoolFromBranches, and then wires up the conditional branch
///   from @p cond back at the origin before resuming in the join block.
/// - Emitted IL: Allocates a temporary boolean slot, lets @p emitThen and
///   @p emitElse populate it via @ref emitStore, and finally emits a
///   conditional branch via @ref emitCBr.
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
    ProcedureContext &ctx = context();
    BasicBlock *origin = ctx.current();
    BasicBlock *thenBlk = nullptr;
    BasicBlock *elseBlk = nullptr;

    std::string_view thenBase = thenLabelBase.empty() ? std::string_view("bool_then") : thenLabelBase;
    std::string_view elseBase = elseLabelBase.empty() ? std::string_view("bool_else") : elseLabelBase;
    std::string_view joinBase = joinLabelBase.empty() ? std::string_view("bool_join") : joinLabelBase;

    IlValue result = emitBoolFromBranches(
        [&](Value slot) {
            thenBlk = ctx.current();
            emitThen(slot);
        },
        [&](Value slot) {
            elseBlk = ctx.current();
            emitElse(slot);
        },
        thenBase,
        elseBase,
        joinBase);

    assert(thenBlk && elseBlk);

    BasicBlock *joinBlk = ctx.current();

    ctx.setCurrent(origin);
    curLoc = loc;
    emitCBr(cond, thenBlk, elseBlk);
    ctx.setCurrent(joinBlk);
    curLoc = loc;
    Value logical = emitBasicLogicalI64(result);
    return {logical, Type(Type::Kind::I64)};
}

Lowerer::Value Lowerer::emitConstI64(std::int64_t v)
{
    return Value::constInt(v);
}

Lowerer::Value Lowerer::emitZext1ToI64(Value val)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), val);
}

Lowerer::Value Lowerer::emitISub(Value lhs, Value rhs)
{
    return emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), lhs, rhs);
}

Lowerer::Value Lowerer::emitBasicLogicalI64(Value b1)
{
    if (context().current() == nullptr)
    {
        if (b1.kind == Value::Kind::ConstInt)
        {
            return Value::constInt(b1.i64 != 0 ? -1 : 0);
        }
        return Value::constInt(0);
    }
    Value i64zero = emitConstI64(0);
    Value zext = emitZext1ToI64(b1);
    return emitISub(i64zero, zext);
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
    RVal val = lowerExpr(*u.expr);
    RVal condVal = coerceToBool(std::move(val), u.loc);
    curLoc = u.loc;
    Value cond = condVal.value;
    return lowerBoolBranchExpr(
        cond,
        u.loc,
        [&](Value slot) {
            curLoc = u.loc;
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
        },
        [&](Value slot) {
            curLoc = u.loc;
            emitStore(ilBoolTy(), slot, emitBoolConst(true));
        });
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
Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    curLoc = b.loc;

    auto toBool = [&](RVal val) {
        return coerceToBool(std::move(val), b.loc).value;
    };

    if (b.op == BinaryExpr::Op::LogicalAndShort)
    {
        Value cond = toBool(lhs);
        return lowerBoolBranchExpr(
            cond,
            b.loc,
            [&](Value slot) {
                RVal rhs = lowerExpr(*b.rhs);
                Value rhsBool = toBool(rhs);
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, rhsBool);
            },
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, emitBoolConst(false));
            },
            "and_rhs",
            "and_false",
            "and_done");
    }

    if (b.op == BinaryExpr::Op::LogicalOrShort)
    {
        Value cond = toBool(lhs);
        return lowerBoolBranchExpr(
            cond,
            b.loc,
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, emitBoolConst(true));
            },
            [&](Value slot) {
                RVal rhs = lowerExpr(*b.rhs);
                Value rhsBool = toBool(rhs);
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, rhsBool);
            },
            "or_true",
            "or_rhs",
            "or_done");
    }

    if (b.op == BinaryExpr::Op::LogicalAnd)
    {
        Value lhsBool = toBool(lhs);
        RVal rhs = lowerExpr(*b.rhs);
        Value rhsBool = toBool(rhs);
        curLoc = b.loc;
        Value lhsLogical = emitBasicLogicalI64(lhsBool);
        curLoc = b.loc;
        Value rhsLogical = emitBasicLogicalI64(rhsBool);
        curLoc = b.loc;
        Value res = emitBinary(Opcode::And, Type(Type::Kind::I64), lhsLogical, rhsLogical);
        return {res, Type(Type::Kind::I64)};
    }

    if (b.op == BinaryExpr::Op::LogicalOr)
    {
        Value lhsBool = toBool(lhs);
        RVal rhs = lowerExpr(*b.rhs);
        Value rhsBool = toBool(rhs);
        curLoc = b.loc;
        Value lhsLogical = emitBasicLogicalI64(lhsBool);
        curLoc = b.loc;
        Value rhsLogical = emitBasicLogicalI64(rhsBool);
        curLoc = b.loc;
        Value res = emitBinary(Opcode::Or, Type(Type::Kind::I64), lhsLogical, rhsLogical);
        return {res, Type(Type::Kind::I64)};
    }

    if (auto *emitter = diagnosticEmitter())
    {
        std::string_view opText = logicalOperatorDisplayName(b.op);
        std::string message = "unsupported logical operator '";
        message.append(opText);
        message.push_back('\'');
        if (opText == std::string_view("<logical>"))
        {
            message.append(" (enum value ");
            message.append(std::to_string(static_cast<int>(b.op)));
            message.push_back(')');
        }
        message.append("; assuming FALSE");
        emitter->emit(il::support::Severity::Error,
                      std::string(kDiagUnsupportedLogicalOperator),
                      b.loc,
                      0,
                      std::move(message));
    }

    curLoc = b.loc;
    Value logicalFalse = emitBasicLogicalI64(emitBoolConst(false));
    return {logicalFalse, Type(Type::Kind::I64)};
}

/// @brief Lower integer division and modulo with width-aware narrowing.
/// @param b Binary expression describing the operation.
/// @return Resulting integer value alongside its IL type.
/// @details
/// - Control flow: Runs linearly in the current block, relying on the
///   checked `sdiv`/`srem` opcodes for divide-by-zero and overflow traps.
/// - Emitted IL: Classifies the operand ranks using AST hints and narrows the
///   values to either `i16` or `i32` with `cast.si_narrow.chk` before issuing
///   the selected arithmetic instruction.
/// - Side effects: Updates @ref curLoc for each emitted narrowing so traps are
///   attributed to the original expression location.
Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);

    std::function<std::optional<Type::Kind>(const Expr &, const RVal &)> classifyIntegerRank;
    classifyIntegerRank = [&](const Expr &expr, const RVal &val) -> std::optional<Type::Kind> {
        using Kind = Type::Kind;
        switch (val.type.kind)
        {
            case Kind::I16:
                return Kind::I16;
            case Kind::I32:
                return Kind::I32;
            case Kind::F64:
            case Kind::Str:
            case Kind::Ptr:
            case Kind::I1:
            case Kind::Error:
            case Kind::ResumeTok:
            case Kind::Void:
                return std::nullopt;
            case Kind::I64:
                break;
        }

        if (const auto *intLit = dynamic_cast<const IntExpr *>(&expr))
        {
            if (intLit->value >= std::numeric_limits<int16_t>::min() &&
                intLit->value <= std::numeric_limits<int16_t>::max())
                return Kind::I16;
            if (intLit->value >= std::numeric_limits<int32_t>::min() &&
                intLit->value <= std::numeric_limits<int32_t>::max())
                return Kind::I32;
            return std::nullopt;
        }
        if (const auto *var = dynamic_cast<const VarExpr *>(&expr))
        {
            if (const auto *info = findSymbol(var->name))
            {
                if (info->hasType)
                {
                    if (info->type == AstType::F64)
                        return std::nullopt;
                }
            }
            AstType astTy = inferAstTypeFromName(var->name);
            if (astTy == AstType::F64)
                return std::nullopt;
            return Kind::I16;
        }
        if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr))
        {
            if (unary->expr)
                return classifyIntegerRank(*unary->expr, val);
        }
        return std::nullopt;
    };

    auto narrowTo = [&](const Expr &expr, RVal v, Type target) {
        if (v.type.kind == target.kind)
            return v;
        v = coerceToI64(std::move(v), expr.loc);
        curLoc = expr.loc;
        v.value = emitUnary(Opcode::CastSiNarrowChk, target, v.value);
        v.type = target;
        return v;
    };

    std::optional<Type::Kind> lhsRank = classifyIntegerRank(*b.lhs, lhs);
    std::optional<Type::Kind> rhsRank = classifyIntegerRank(*b.rhs, rhs);

    Opcode op = (b.op == BinaryExpr::Op::IDiv) ? Opcode::SDivChk0 : Opcode::SRemChk0;
    Type resultTy(Type::Kind::I64);

    if (lhsRank && rhsRank)
    {
        Type::Kind promoted = (*lhsRank == Type::Kind::I32 || *rhsRank == Type::Kind::I32)
                                  ? Type::Kind::I32
                                  : Type::Kind::I16;
        resultTy = Type(promoted);
        lhs = narrowTo(*b.lhs, std::move(lhs), resultTy);
        rhs = narrowTo(*b.rhs, std::move(rhs), resultTy);
    }
    else
    {
        lhs = coerceToI64(std::move(lhs), b.loc);
        rhs = coerceToI64(std::move(rhs), b.loc);
        resultTy = Type(Type::Kind::I64);
    }

    curLoc = b.loc;
    Value res = emitBinary(op, resultTy, lhs.value, rhs.value);
    return {res, resultTy};
}

/// @brief Lower string binary operations, mapping to runtime helpers.
/// @param b Binary expression describing a string operator.
/// @param lhs Left-hand operand already lowered to IL.
/// @param rhs Right-hand operand already lowered to IL.
/// @return Lowered value with either string or boolean type.
/// @details
/// - Control flow: Runs linearly within the current block with no new
///   branches.
/// - Emitted IL: Invokes runtime routines such as `rt_concat` and
///   `rt_str_eq`, including boolean negation when handling inequality.
/// - Side effects: Updates @ref curLoc prior to the call so string helper
///   diagnostics report the proper source span.
Lowerer::RVal Lowerer::lowerStringBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (b.op == BinaryExpr::Op::Add)
    {
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_concat", {lhs.value, rhs.value});
        return {res, Type(Type::Kind::Str)};
    }
    Value eq = emitCallRet(ilBoolTy(), "rt_str_eq", {lhs.value, rhs.value});
    Value eqLogical = emitBasicLogicalI64(eq);
    if (b.op == BinaryExpr::Op::Ne)
    {
        curLoc = b.loc;
        Value res = emitBinary(Opcode::Xor, Type(Type::Kind::I64), eqLogical, emitConstI64(-1));
        return {res, Type(Type::Kind::I64)};
    }
    return {eqLogical, Type(Type::Kind::I64)};
}

Lowerer::RVal Lowerer::lowerPowBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    il::support::SourceLoc lhsLoc = b.lhs ? b.lhs->loc : b.loc;
    il::support::SourceLoc rhsLoc = b.rhs ? b.rhs->loc : b.loc;
    lhs = ensureF64(std::move(lhs), lhsLoc);
    rhs = ensureF64(std::move(rhs), rhsLoc);
    trackRuntime(RuntimeFeature::Pow);
    curLoc = b.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_pow_f64_chkdom", {lhs.value, rhs.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower numeric binary expressions, promoting operands as needed.
/// @param b Arithmetic or comparison expression to translate.
/// @param lhs Lowered left-hand operand.
/// @param rhs Lowered right-hand operand.
/// @return Result of the operation with either numeric or boolean type.
/// @details
/// - Control flow: Executes in a straight line without creating additional
///   blocks.
/// - Emitted IL: Inserts integer-to-float conversions when operand types
///   differ and chooses among arithmetic or comparison opcodes before issuing
///   a single binary instruction.
/// - Side effects: Updates @ref curLoc and mutates the temporary
///   @ref Lowerer::RVal structures to reflect promotions.
Lowerer::RVal Lowerer::lowerNumericBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (b.op == BinaryExpr::Op::Div)
    {
        auto promoteToF64 = [&](RVal value, const Expr &expr) {
            if (value.type.kind == Type::Kind::F64)
                return value;
            value = coerceToI64(std::move(value), expr.loc);
            curLoc = expr.loc;
            value.value = emitUnary(Opcode::CastSiToFp, Type(Type::Kind::F64), value.value);
            value.type = Type(Type::Kind::F64);
            return value;
        };

        if (b.lhs)
            lhs = promoteToF64(std::move(lhs), *b.lhs);
        else
            lhs = promoteToF64(std::move(lhs), b);
        if (b.rhs)
            rhs = promoteToF64(std::move(rhs), *b.rhs);
        else
            rhs = promoteToF64(std::move(rhs), b);

        curLoc = b.loc;
        Value res = emitBinary(Opcode::FDiv, Type(Type::Kind::F64), lhs.value, rhs.value);
        return {res, Type(Type::Kind::F64)};
    }

    if (lhs.type.kind == Type::Kind::I64 && rhs.type.kind == Type::Kind::F64)
    {
        lhs = coerceToF64(std::move(lhs), b.loc);
    }
    else if (lhs.type.kind == Type::Kind::F64 && rhs.type.kind == Type::Kind::I64)
    {
        rhs = coerceToF64(std::move(rhs), b.loc);
    }
    auto isIntegerKind = [](Type::Kind kind) {
        return kind == Type::Kind::I16 || kind == Type::Kind::I32 || kind == Type::Kind::I64;
    };
    bool isFloat = lhs.type.kind == Type::Kind::F64;
    if (!isFloat && b.op == BinaryExpr::Op::Sub)
    {
        if (const auto *lhsInt = dynamic_cast<const IntExpr *>(b.lhs.get());
            lhsInt && lhsInt->value == 0 && isIntegerKind(rhs.type.kind) &&
            (rhs.type.kind == Type::Kind::I16 || rhs.type.kind == Type::Kind::I32))
        {
            Value neg = emitCheckedNeg(rhs.type, rhs.value);
            return {neg, rhs.type};
        }
    }
    auto integerArithmeticType = [](Type::Kind lhsKind, Type::Kind rhsKind) {
        if (lhsKind == Type::Kind::I16 && rhsKind == Type::Kind::I16)
            return Type(Type::Kind::I16);
        if (lhsKind == Type::Kind::I32 && rhsKind == Type::Kind::I32)
            return Type(Type::Kind::I32);
        return Type(Type::Kind::I64);
    };
    Opcode op = Opcode::IAddOvf;
    Type arithTy = isFloat ? Type(Type::Kind::F64)
                           : integerArithmeticType(lhs.type.kind, rhs.type.kind);
    if (!isFloat)
    {
        const auto *lhsInt = dynamic_cast<const IntExpr *>(b.lhs.get());
        const auto *rhsInt = dynamic_cast<const IntExpr *>(b.rhs.get());
        if (lhsInt && rhsInt)
        {
            const auto fits16 = [](long long v) {
                return v >= std::numeric_limits<int16_t>::min() && v <= std::numeric_limits<int16_t>::max();
            };
            const auto fits32 = [](long long v) {
                return v >= std::numeric_limits<int32_t>::min() && v <= std::numeric_limits<int32_t>::max();
            };
            if (fits16(lhsInt->value) && fits16(rhsInt->value))
            {
                arithTy = Type(Type::Kind::I16);
            }
            else if (fits32(lhsInt->value) && fits32(rhsInt->value))
            {
                arithTy = Type(Type::Kind::I32);
            }
        }
    }
    Type ty = arithTy;
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
            op = isFloat ? Opcode::FAdd : Opcode::IAddOvf;
            break;
        case BinaryExpr::Op::Sub:
            op = isFloat ? Opcode::FSub : Opcode::ISubOvf;
            break;
        case BinaryExpr::Op::Mul:
            op = isFloat ? Opcode::FMul : Opcode::IMulOvf;
            break;
        case BinaryExpr::Op::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDivChk0;
            ty = isFloat ? arithTy : Type(Type::Kind::I64);
            break;
        case BinaryExpr::Op::Eq:
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Ne:
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            ty = ilBoolTy();
            break;
        case BinaryExpr::Op::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            ty = ilBoolTy();
            break;
        default:
            break; // other ops handled elsewhere
    }
    Value res = emitBinary(op, ty, lhs.value, rhs.value);
    if (ty.kind == Type::Kind::I1)
    {
        curLoc = b.loc;
        Value logical = emitBasicLogicalI64(res);
        return {logical, Type(Type::Kind::I64)};
    }
    return {res, ty};
}

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
    if ((b.op == BinaryExpr::Op::Add || b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne) &&
        lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        return lowerStringBinary(b, lhs, rhs);
    return lowerNumericBinary(b, lhs, rhs);
}

/// @brief Coerce a value into a 64-bit integer representation.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `i64` type when conversion occurs.
Lowerer::RVal Lowerer::coerceToI64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::I1)
    {
        curLoc = loc;
        v.value = emitBasicLogicalI64(v.value);
        v.type = Type(Type::Kind::I64);
    }
    else if (v.type.kind == Type::Kind::F64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    else if (v.type.kind == Type::Kind::I16 || v.type.kind == Type::Kind::I32)
    {
        v.type = Type(Type::Kind::I64);
    }
    return v;
}

/// @brief Coerce a value into a 64-bit floating-point representation.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `f64` type when conversion occurs.
Lowerer::RVal Lowerer::coerceToF64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::F64)
        return v;
    v = coerceToI64(std::move(v), loc);
    if (v.type.kind == Type::Kind::I64)
    {
        curLoc = loc;
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
    if (v.type.kind == Type::Kind::I1)
        return v;
    if (v.type.kind == Type::Kind::F64 || v.type.kind == Type::Kind::I16 || v.type.kind == Type::Kind::I32 ||
        v.type.kind == Type::Kind::I64)
        v = coerceToI64(std::move(v), loc);
    if (v.type.kind != Type::Kind::I1)
    {
        curLoc = loc;
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

/// @brief Lower a BASIC builtin call using declarative metadata.
/// @param c Builtin call AST node to translate.
/// @return Lowered value and its IL type.
/// @details
/// - Control flow: Follows the first matching metadata variant, which may emit
///   straight-line code or runtime calls depending on argument presence and
///   types.
/// - Emitted IL: Applies argument transforms (coercions, adjustments) prior to
///   issuing runtime calls or unary conversions.
/// - Side effects: Records runtime helper usage according to the variant's
///   feature actions.
Lowerer::RVal Lowerer::lowerBuiltinCall(const BuiltinCallExpr &c)
{
    const auto &rule = getBuiltinLoweringRule(c.builtin);

    if (c.builtin == BuiltinCallExpr::Builtin::Str)
    {
        if (c.args.empty() || !c.args[0])
            return {Value::constInt(0), Type(Type::Kind::Str)};

        RVal argVal = lowerExpr(*c.args[0]);
        TypeRules::NumericType numericType = classifyNumericType(*c.args[0]);
        const il::support::SourceLoc argLoc = c.args[0]->loc;

        const char *runtime = nullptr;
        RuntimeFeature feature = RuntimeFeature::StrFromDouble;

        auto narrowInteger = [&](Type::Kind target) {
            if (argVal.type.kind != target)
            {
                argVal = coerceToI64(std::move(argVal), argLoc);
                curLoc = argLoc;
                argVal.value = emitUnary(Opcode::CastSiNarrowChk, Type(target), argVal.value);
                argVal.type = Type(target);
            }
        };

        switch (numericType)
        {
            case TypeRules::NumericType::Integer:
                runtime = "rt_str_i16_alloc";
                feature = RuntimeFeature::StrFromI16;
                narrowInteger(Type::Kind::I16);
                break;
            case TypeRules::NumericType::Long:
                runtime = "rt_str_i32_alloc";
                feature = RuntimeFeature::StrFromI32;
                narrowInteger(Type::Kind::I32);
                break;
            case TypeRules::NumericType::Single:
                runtime = "rt_str_f_alloc";
                feature = RuntimeFeature::StrFromSingle;
                argVal = ensureF64(std::move(argVal), argLoc);
                break;
            case TypeRules::NumericType::Double:
            default:
                runtime = "rt_str_d_alloc";
                feature = RuntimeFeature::StrFromDouble;
                argVal = ensureF64(std::move(argVal), argLoc);
                break;
        }

        requestHelper(feature);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::Str), runtime, {argVal.value});
        return {res, Type(Type::Kind::Str)};
    }

    std::vector<std::optional<ExprType>> originalTypes(c.args.size());
    std::vector<std::optional<il::support::SourceLoc>> argLocs(c.args.size());
    for (std::size_t i = 0; i < c.args.size(); ++i)
    {
        const auto &arg = c.args[i];
        if (!arg)
            continue;
        argLocs[i] = arg->loc;
        originalTypes[i] = scanExpr(*arg);
    }

    auto hasArg = [&](std::size_t idx) -> bool {
        return idx < c.args.size() && c.args[idx] != nullptr;
    };

    std::vector<std::optional<RVal>> loweredArgs(c.args.size());
    std::vector<RVal> syntheticArgs;

    const auto *variant = static_cast<const BuiltinLoweringRule::Variant *>(nullptr);
    for (const auto &candidate : rule.variants)
    {
        bool matches = false;
        switch (candidate.condition)
        {
            case BuiltinLoweringRule::Variant::Condition::Always:
                matches = true;
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgPresent:
                matches = hasArg(candidate.conditionArg);
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgMissing:
                matches = !hasArg(candidate.conditionArg);
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgTypeIs:
                if (hasArg(candidate.conditionArg) && originalTypes[candidate.conditionArg])
                    matches = *originalTypes[candidate.conditionArg] == candidate.conditionType;
                break;
            case BuiltinLoweringRule::Variant::Condition::IfArgTypeIsNot:
                if (hasArg(candidate.conditionArg) && originalTypes[candidate.conditionArg])
                    matches = *originalTypes[candidate.conditionArg] != candidate.conditionType;
                break;
        }
        if (matches)
        {
            variant = &candidate;
            break;
        }
    }

    if (!variant && !rule.variants.empty())
        variant = &rule.variants.front();
    if (!variant)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    auto ensureLoweredIndex = [&](std::size_t idx) -> RVal & {
        assert(hasArg(idx) && "builtin lowering referenced missing argument");
        auto &slot = loweredArgs[idx];
        if (!slot)
            slot = lowerExpr(*c.args[idx]);
        return *slot;
    };

    auto typeFromExpr = [&](ExprType expr) -> Type {
        switch (expr)
        {
            case ExprType::F64:
                return Type(Type::Kind::F64);
            case ExprType::Str:
                return Type(Type::Kind::Str);
            case ExprType::Bool:
                return ilBoolTy();
            case ExprType::I64:
            default:
                return Type(Type::Kind::I64);
        }
    };

    auto resolveResultType = [&]() -> Type {
        switch (rule.result.kind)
        {
            case BuiltinLoweringRule::ResultSpec::Kind::Fixed:
                return typeFromExpr(rule.result.type);
            case BuiltinLoweringRule::ResultSpec::Kind::FromArg:
            {
                std::size_t idx = rule.result.argIndex;
                if (hasArg(idx))
                    return ensureLoweredIndex(idx).type;
                return typeFromExpr(rule.result.type);
            }
        }
        return Type(Type::Kind::I64);
    };

    auto ensureLoweredArgument = [&](const BuiltinLoweringRule::Argument &argSpec) -> RVal & {
        std::size_t idx = argSpec.index;
        if (idx < c.args.size() && c.args[idx])
            return ensureLoweredIndex(idx);
        if (argSpec.defaultValue)
        {
            const auto &def = *argSpec.defaultValue;
            RVal value{Value::constInt(def.i64), Type(Type::Kind::I64)};
            switch (def.type)
            {
                case ExprType::F64:
                    value = RVal{Value::constFloat(def.f64), Type(Type::Kind::F64)};
                    break;
                case ExprType::Str:
                    assert(false && "string default values are not supported");
                    break;
                case ExprType::Bool:
                    value = RVal{emitBoolConst(def.i64 != 0), ilBoolTy()};
                    break;
                case ExprType::I64:
                default:
                    value = RVal{Value::constInt(def.i64), Type(Type::Kind::I64)};
                    break;
            }
            syntheticArgs.push_back(value);
            return syntheticArgs.back();
        }
        assert(false && "builtin lowering referenced missing argument without default");
        syntheticArgs.emplace_back(Value::constInt(0), Type(Type::Kind::I64));
        return syntheticArgs.back();
    };

    auto selectArgLoc = [&](const BuiltinLoweringRule::Argument &argSpec) -> il::support::SourceLoc {
        if (argSpec.index < argLocs.size() && argLocs[argSpec.index])
            return *argLocs[argSpec.index];
        return c.loc;
    };

    auto applyTransforms = [&](const BuiltinLoweringRule::Argument &argSpec,
                               const std::vector<BuiltinLoweringRule::ArgTransform> &transforms) -> RVal & {
        RVal &slot = ensureLoweredArgument(argSpec);
        il::support::SourceLoc loc = selectArgLoc(argSpec);
        for (const auto &transform : transforms)
        {
            switch (transform.kind)
            {
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureI64:
                    slot = ensureI64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureF64:
                    slot = ensureF64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::EnsureI32:
                    slot = ensureI64(std::move(slot), loc);
                    if (slot.type.kind != Type::Kind::I32)
                    {
                        curLoc = loc;
                        slot.value = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), slot.value);
                        slot.type = Type(Type::Kind::I32);
                    }
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceI64:
                    slot = coerceToI64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceF64:
                    slot = coerceToF64(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::CoerceBool:
                    slot = coerceToBool(std::move(slot), loc);
                    break;
                case BuiltinLoweringRule::ArgTransform::Kind::AddConst:
                    curLoc = loc;
                    slot.value = emitBinary(
                        Opcode::IAddOvf, Type(Type::Kind::I64), slot.value, Value::constInt(transform.immediate));
                    slot.type = Type(Type::Kind::I64);
                    break;
            }
        }
        return slot;
    };

    auto selectCallLoc = [&](const std::optional<std::size_t> &idx) -> il::support::SourceLoc {
        if (idx && *idx < argLocs.size() && argLocs[*idx])
        {
            return *argLocs[*idx];
        }
        return c.loc;
    };

    Value resultValue = Value::constInt(0);
    Type resultType = Type(Type::Kind::I64);

    switch (variant->kind)
    {
        case BuiltinLoweringRule::Variant::Kind::CallRuntime:
        {
            std::vector<Value> callArgs;
            callArgs.reserve(variant->arguments.size());
            for (const auto &argSpec : variant->arguments)
            {
                RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
                callArgs.push_back(argVal.value);
            }
            resultType = resolveResultType();
            curLoc = selectCallLoc(variant->callLocArg);
            resultValue = emitCallRet(resultType, variant->runtime, callArgs);
            break;
        }
        case BuiltinLoweringRule::Variant::Kind::EmitUnary:
        {
            assert(!variant->arguments.empty() && "unary builtin requires an operand");
            const auto &argSpec = variant->arguments.front();
            RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
            resultType = resolveResultType();
            curLoc = selectCallLoc(variant->callLocArg);
            resultValue = emitUnary(variant->opcode, resultType, argVal.value);
            break;
        }
        case BuiltinLoweringRule::Variant::Kind::Custom:
        {
            assert(!variant->arguments.empty() && "custom builtin requires an operand");
            const auto &argSpec = variant->arguments.front();
            RVal &argVal = applyTransforms(argSpec, argSpec.transforms);
            il::support::SourceLoc callLoc = selectCallLoc(variant->callLocArg);

            auto handleConversion = [&](Type resultTy) {
                Value okSlot = emitAlloca(1);
                std::vector<Value> callArgs{argVal.value, okSlot};
                resultType = resultTy;
                curLoc = callLoc;
                Value callRes = emitCallRet(resultType, variant->runtime, callArgs);

                curLoc = callLoc;
                Value okVal = emitLoad(ilBoolTy(), okSlot);
                ProcedureContext &ctx = context();
                Function *func = ctx.function();
                assert(func && "conversion lowering requires active function");
                BasicBlock *origin = ctx.current();
                assert(origin && "conversion lowering requires active block");
                std::string originLabel = origin->label;
                BlockNamer *blockNamer = ctx.blockNames().namer();
                std::string contLabel = blockNamer ? blockNamer->generic("conv_ok")
                                                   : mangler.block("conv_ok");
                std::string trapLabel = blockNamer ? blockNamer->generic("conv_trap")
                                                   : mangler.block("conv_trap");
                BasicBlock *contBlk = &builder->addBlock(*func, contLabel);
                BasicBlock *trapBlk = &builder->addBlock(*func, trapLabel);
                auto originIt = std::find_if(
                    func->blocks.begin(), func->blocks.end(), [&](const BasicBlock &bb) {
                        return bb.label == originLabel;
                    });
                assert(originIt != func->blocks.end());
                origin = &*originIt;
                ctx.setCurrent(origin);
                emitCBr(okVal, contBlk, trapBlk);

                ctx.setCurrent(trapBlk);
                curLoc = callLoc;
                Value sentinel = emitUnary(Opcode::CastFpToSiRteChk,
                                           Type(Type::Kind::I64),
                                           Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
                (void)sentinel;
                emitTrap();

                ctx.setCurrent(contBlk);
                resultValue = callRes;
            };

            switch (c.builtin)
            {
                case BuiltinCallExpr::Builtin::Cint:
                    handleConversion(Type(Type::Kind::I64));
                    break;
                case BuiltinCallExpr::Builtin::Clng:
                    handleConversion(Type(Type::Kind::I64));
                    break;
                case BuiltinCallExpr::Builtin::Csng:
                    handleConversion(Type(Type::Kind::F64));
                    break;
                case BuiltinCallExpr::Builtin::Val:
                {
                    il::support::SourceLoc callLoc = selectCallLoc(variant->callLocArg);
                    curLoc = callLoc;
                    Value cstr = emitCallRet(Type(Type::Kind::Ptr), "rt_string_cstr", {argVal.value});

                    Value okSlot = emitAlloca(1);
                    std::vector<Value> callArgs{cstr, okSlot};
                    resultType = resolveResultType();
                    curLoc = callLoc;
                    Value callRes = emitCallRet(resultType, variant->runtime, callArgs);

                    curLoc = callLoc;
                    Value okVal = emitLoad(ilBoolTy(), okSlot);

                    ProcedureContext &ctx = context();
                    Function *func = ctx.function();
                    assert(func && "VAL lowering requires active function");
                    BasicBlock *origin = ctx.current();
                    assert(origin && "VAL lowering requires active block");
                    std::string originLabel = origin->label;
                    BlockNamer *blockNamer = ctx.blockNames().namer();
                    auto labelFor = [&](const char *hint) {
                        if (blockNamer)
                            return blockNamer->generic(hint);
                        return mangler.block(std::string(hint));
                    };
                    std::string contLabel = labelFor("val_ok");
                    std::string trapLabel = labelFor("val_fail");
                    std::string nanLabel = labelFor("val_nan");
                    std::string overflowLabel = labelFor("val_over");
                    builder->addBlock(*func, contLabel);
                    builder->addBlock(*func, trapLabel);
                    builder->addBlock(*func, nanLabel);
                    builder->addBlock(*func, overflowLabel);

                    auto originIt = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const BasicBlock &bb) {
                        return bb.label == originLabel;
                    });
                    assert(originIt != func->blocks.end());
                    origin = &*originIt;
                    auto findBlock = [&](const std::string &label) {
                        auto it = std::find_if(func->blocks.begin(), func->blocks.end(), [&](const BasicBlock &bb) {
                            return bb.label == label;
                        });
                        assert(it != func->blocks.end());
                        return &*it;
                    };
                    BasicBlock *contBlk = findBlock(contLabel);
                    BasicBlock *trapBlk = findBlock(trapLabel);
                    BasicBlock *nanBlk = findBlock(nanLabel);
                    BasicBlock *overflowBlk = findBlock(overflowLabel);
                    ctx.setCurrent(origin);
                    curLoc = callLoc;
                    emitCBr(okVal, contBlk, trapBlk);

                    ctx.setCurrent(trapBlk);
                    curLoc = callLoc;
                    Value isNan = emitBinary(Opcode::FCmpNE, ilBoolTy(), callRes, callRes);
                    emitCBr(isNan, nanBlk, overflowBlk);

                    ctx.setCurrent(nanBlk);
                    curLoc = callLoc;
                    Value invalidSentinel = emitUnary(Opcode::CastFpToSiRteChk,
                                                       Type(Type::Kind::I64),
                                                       Value::constFloat(std::numeric_limits<double>::quiet_NaN()));
                    (void)invalidSentinel;
                    emitTrap();

                    ctx.setCurrent(overflowBlk);
                    curLoc = callLoc;
                    Value overflowSentinel = emitUnary(Opcode::CastFpToSiRteChk,
                                                        Type(Type::Kind::I64),
                                                        Value::constFloat(std::numeric_limits<double>::max()));
                    (void)overflowSentinel;
                    emitTrap();

                    ctx.setCurrent(contBlk);
                    resultValue = callRes;
                    break;
                }
                default:
                    assert(false && "unsupported custom builtin conversion");
                    return {Value::constInt(0), Type(Type::Kind::I64)};
            }
            break;
        }
        default:
        {
            auto variantKindName = [&]() -> std::string {
                switch (variant->kind)
                {
                    case BuiltinLoweringRule::Variant::Kind::CallRuntime:
                        return "CallRuntime";
                    case BuiltinLoweringRule::Variant::Kind::EmitUnary:
                        return "EmitUnary";
                    case BuiltinLoweringRule::Variant::Kind::Custom:
                        return "Custom";
                }
                std::string unknown = "<unknown (";
                unknown.append(std::to_string(static_cast<int>(variant->kind)));
                unknown.push_back(')');
                return unknown;
            };

            if (auto *emitter = diagnosticEmitter())
            {
                std::string message = "custom builtin lowering variant is not supported: ";
                message.append(variantKindName());
                emitter->emit(il::support::Severity::Error,
                              std::string(kDiagUnsupportedCustomBuiltinVariant),
                              selectCallLoc(variant->callLocArg),
                              0,
                              std::move(message));
            }

            resultValue = Value::constInt(0);
            resultType = Type(Type::Kind::I64);
            break;
        }
    }

    for (const auto &feature : variant->features)
    {
        switch (feature.action)
        {
            case BuiltinLoweringRule::Feature::Action::Request:
                requestHelper(feature.feature);
                break;
            case BuiltinLoweringRule::Feature::Action::Track:
                trackRuntime(feature.feature);
                break;
        }
    }

    return {resultValue, resultType};
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
Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    LowererExprVisitor visitor(*this);
    expr.accept(visitor);
    return visitor.result();
}

} // namespace il::frontends::basic

