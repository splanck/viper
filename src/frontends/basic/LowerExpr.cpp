// File: src/frontends/basic/LowerExpr.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements expression lowering helpers for the BASIC front end.
// Key invariants: Expression lowering preserves operand types, injecting
//                 conversions to match IL expectations and runtime helpers.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

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
        result_ = Lowerer::RVal{lowerer_.emitBoolConst(expr.value), lowerer_.ilBoolTy()};
    }

    void visit(const VarExpr &expr) override { result_ = lowerer_.lowerVarExpr(expr); }

    void visit(const ArrayExpr &expr) override
    {
        Value ptr = lowerer_.lowerArrayAddr(expr);
        lowerer_.curLoc = expr.loc;
        Value val = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::I64), ptr);
        result_ = Lowerer::RVal{val, il::core::Type(il::core::Type::Kind::I64)};
    }

    void visit(const UnaryExpr &expr) override { result_ = lowerer_.lowerUnaryExpr(expr); }

    void visit(const BinaryExpr &expr) override { result_ = lowerer_.lowerBinaryExpr(expr); }

    void visit(const BuiltinCallExpr &expr) override
    {
        result_ = lowerer_.lowerBuiltinCall(expr);
    }

    void visit(const CallExpr &expr) override
    {
        const Function *callee = nullptr;
        if (lowerer_.mod)
        {
            for (const auto &f : lowerer_.mod->functions)
                if (f.name == expr.callee)
                {
                    callee = &f;
                    break;
                }
        }
        std::vector<Value> args;
        args.reserve(expr.args.size());
        for (size_t i = 0; i < expr.args.size(); ++i)
        {
            Lowerer::RVal arg = lowerer_.lowerExpr(*expr.args[i]);
            if (callee && i < callee->params.size())
            {
                il::core::Type paramTy = callee->params[i].type;
                if (paramTy.kind == il::core::Type::Kind::F64 &&
                    arg.type.kind == il::core::Type::Kind::I64)
                {
                    lowerer_.curLoc = expr.loc;
                    arg.value =
                        lowerer_.emitUnary(Opcode::Sitofp,
                                           il::core::Type(il::core::Type::Kind::F64),
                                           arg.value);
                    arg.type = il::core::Type(il::core::Type::Kind::F64);
                }
                else if (paramTy.kind == il::core::Type::Kind::F64 &&
                         arg.type.kind == il::core::Type::Kind::I1)
                {
                    lowerer_.curLoc = expr.loc;
                    arg.value =
                        lowerer_.emitUnary(Opcode::Zext1,
                                           il::core::Type(il::core::Type::Kind::I64),
                                           arg.value);
                    arg.value =
                        lowerer_.emitUnary(Opcode::Sitofp,
                                           il::core::Type(il::core::Type::Kind::F64),
                                           arg.value);
                    arg.type = il::core::Type(il::core::Type::Kind::F64);
                }
                else if (paramTy.kind == il::core::Type::Kind::I64 &&
                         arg.type.kind == il::core::Type::Kind::I1)
                {
                    lowerer_.curLoc = expr.loc;
                    arg.value =
                        lowerer_.emitUnary(Opcode::Zext1,
                                           il::core::Type(il::core::Type::Kind::I64),
                                           arg.value);
                    arg.type = il::core::Type(il::core::Type::Kind::I64);
                }
            }
            args.push_back(arg.value);
        }
        lowerer_.curLoc = expr.loc;
        if (callee && callee->retType.kind != il::core::Type::Kind::Void)
        {
            Value res = lowerer_.emitCallRet(callee->retType, expr.callee, args);
            result_ = Lowerer::RVal{res, callee->retType};
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
/// - Emitted IL: Issues a load from the stack slot recorded in
///   @ref varSlots, selecting pointer, string, floating, or boolean types as
///   required.
/// - Side effects: Updates @ref curLoc so diagnostics and subsequent
///   instructions are tagged with @p v's source location.
Lowerer::RVal Lowerer::lowerVarExpr(const VarExpr &v)
{
    curLoc = v.loc;
    auto it = varSlots.find(v.name);
    assert(it != varSlots.end());
    Value ptr = Value::temp(it->second);
    bool isArray = arrays.count(v.name);
    bool isStr = !v.name.empty() && v.name.back() == '$';
    bool isF64 = !v.name.empty() && v.name.back() == '#';
    bool isBoolVar = false;
    auto typeIt = varTypes.find(v.name);
    if (typeIt != varTypes.end() && typeIt->second == AstType::Bool)
        isBoolVar = true;
    Type ty = isArray ? Type(Type::Kind::Ptr)
                      : (isStr ? Type(Type::Kind::Str)
                               : (isF64 ? Type(Type::Kind::F64)
                                        : (isBoolVar ? ilBoolTy() : Type(Type::Kind::I64))));
    Value val = emitLoad(ty, ptr);
    return {val, ty};
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
    BasicBlock *origin = cur;
    BasicBlock *thenBlk = nullptr;
    BasicBlock *elseBlk = nullptr;

    std::string_view thenBase = thenLabelBase.empty() ? std::string_view("bool_then") : thenLabelBase;
    std::string_view elseBase = elseLabelBase.empty() ? std::string_view("bool_else") : elseLabelBase;
    std::string_view joinBase = joinLabelBase.empty() ? std::string_view("bool_join") : joinLabelBase;

    IlValue result = emitBoolFromBranches(
        [&](Value slot) {
            thenBlk = cur;
            emitThen(slot);
        },
        [&](Value slot) {
            elseBlk = cur;
            emitElse(slot);
        },
        thenBase,
        elseBase,
        joinBase);

    assert(thenBlk && elseBlk);

    BasicBlock *joinBlk = cur;

    cur = origin;
    curLoc = loc;
    emitCBr(cond, thenBlk, elseBlk);
    cur = joinBlk;
    return {result, ilBoolTy()};
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
    curLoc = u.loc;
    Value cond = val.value;
    if (val.type.kind != Type::Kind::I1)
        cond = emitUnary(Opcode::Trunc1, ilBoolTy(), cond);
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

    auto toBool = [&](const RVal &val) {
        Value v = val.value;
        if (val.type.kind != Type::Kind::I1)
        {
            curLoc = b.loc;
            v = emitUnary(Opcode::Trunc1, ilBoolTy(), v);
        }
        return v;
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
        return lowerBoolBranchExpr(
            lhsBool,
            b.loc,
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, rhsBool);
            },
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, emitBoolConst(false));
            });
    }

    if (b.op == BinaryExpr::Op::LogicalOr)
    {
        Value lhsBool = toBool(lhs);
        RVal rhs = lowerExpr(*b.rhs);
        Value rhsBool = toBool(rhs);
        return lowerBoolBranchExpr(
            lhsBool,
            b.loc,
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, emitBoolConst(true));
            },
            [&](Value slot) {
                curLoc = b.loc;
                emitStore(ilBoolTy(), slot, rhsBool);
            });
    }

    assert(false && "unsupported logical operator");
    return {emitBoolConst(false), ilBoolTy()};
}

/// @brief Lower integer division and modulo with divide-by-zero trapping.
/// @param b Binary expression describing the operation.
/// @return Resulting integer value alongside its IL type.
/// @details
/// - Control flow: Introduces explicit trap and success blocks, branching on
///   a zero-divisor check before emitting the selected arithmetic instruction.
/// - Emitted IL: Generates an `icmp eq` against zero, a `cbr` that targets the
///   trap and ok blocks, a call to @ref emitTrap, and finally either `sdiv` or
///   `srem`.
/// - Side effects: Updates @ref cur while creating additional blocks and
///   records @ref curLoc for diagnostic accuracy.
Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    curLoc = b.loc;
    Value cond = emitBinary(Opcode::ICmpEq, ilBoolTy(), rhs.value, Value::constInt(0));
    std::string trapLbl = blockNamer ? blockNamer->generic("div0") : mangler.block("div0");
    std::string okLbl = blockNamer ? blockNamer->generic("divok") : mangler.block("divok");
    BasicBlock *trapBB = &builder->addBlock(*func, trapLbl);
    BasicBlock *okBB = &builder->addBlock(*func, okLbl);
    emitCBr(cond, trapBB, okBB);
    cur = trapBB;
    curLoc = b.loc;
    emitTrap();
    cur = okBB;
    curLoc = b.loc;
    Opcode op = (b.op == BinaryExpr::Op::IDiv) ? Opcode::SDiv : Opcode::SRem;
    Value res = emitBinary(op, Type(Type::Kind::I64), lhs.value, rhs.value);
    return {res, Type(Type::Kind::I64)};
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
    if (b.op == BinaryExpr::Op::Ne)
    {
        Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eq);
        Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), z, Value::constInt(1));
        Value res = emitUnary(Opcode::Trunc1, ilBoolTy(), x);
        return {res, ilBoolTy()};
    }
    return {eq, ilBoolTy()};
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
    if (lhs.type.kind == Type::Kind::I64 && rhs.type.kind == Type::Kind::F64)
    {
        lhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), lhs.value);
        lhs.type = Type(Type::Kind::F64);
    }
    else if (lhs.type.kind == Type::Kind::F64 && rhs.type.kind == Type::Kind::I64)
    {
        rhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), rhs.value);
        rhs.type = Type(Type::Kind::F64);
    }
    bool isFloat = lhs.type.kind == Type::Kind::F64;
    Opcode op = Opcode::Add;
    Type ty = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
            op = isFloat ? Opcode::FAdd : Opcode::Add;
            break;
        case BinaryExpr::Op::Sub:
            op = isFloat ? Opcode::FSub : Opcode::Sub;
            break;
        case BinaryExpr::Op::Mul:
            op = isFloat ? Opcode::FMul : Opcode::Mul;
            break;
        case BinaryExpr::Op::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDiv;
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
    if ((b.op == BinaryExpr::Op::Add || b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne) &&
        lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        return lowerStringBinary(b, lhs, rhs);
    return lowerNumericBinary(b, lhs, rhs);
}

/// @brief Lower a single builtin argument expression.
/// @param c Builtin invocation currently being lowered.
/// @param idx Index of the argument to translate.
/// @return Lowered argument value and its IL type.
/// @details
/// - Control flow: Executes inline in the current block and simply forwards to
///   @ref lowerExpr.
/// - Emitted IL: Whatever @ref lowerExpr produces for the argument subtree.
/// - Side effects: Validates argument presence via @ref assert and propagates
///   any state changes performed by @ref lowerExpr.
Lowerer::RVal Lowerer::lowerArg(const BuiltinCallExpr &c, size_t idx)
{
    assert(idx < c.args.size() && c.args[idx]);
    return lowerExpr(*c.args[idx]);
}

/// @brief Ensure a value is represented as a 64-bit integer.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `i64` type.
/// @details
/// - Control flow: Executes linearly without creating new blocks.
/// - Emitted IL: Uses @ref emitUnary to sign-extend booleans via `zext` and
///   convert floating-point inputs with `fptosi`.
/// - Side effects: Updates @ref curLoc before emitting conversions and mutates
///   the provided @ref Lowerer::RVal in place.
Lowerer::RVal Lowerer::ensureI64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::I1)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    else if (v.type.kind == Type::Kind::F64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    return v;
}

/// @brief Ensure a value is represented as a 64-bit floating-point number.
/// @param v Value/type pair to normalize.
/// @param loc Source location used for emitted conversions.
/// @return Updated value guaranteed to have `f64` type.
/// @details
/// - Control flow: Executes sequentially, delegating to @ref ensureI64 when a
///   narrowing or widening conversion is required.
/// - Emitted IL: Emits @ref emitUnary instructions for integer-to-float
///   promotion via `sitofp`.
/// - Side effects: Updates @ref curLoc prior to generating conversions and
///   mutates the provided @ref Lowerer::RVal in place.
Lowerer::RVal Lowerer::ensureF64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::F64)
        return v;
    v = ensureI64(std::move(v), loc);
    if (v.type.kind == Type::Kind::I64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
        v.type = Type(Type::Kind::F64);
    }
    return v;
}

/// @brief Lower the RND builtin.
/// @param c Builtin call expression representing `RND`.
/// @return Floating-point result produced by the runtime helper.
/// @details
/// - Control flow: Straight-line emission within the current block.
/// - Emitted IL: Generates a call returning `f64` to the `rt_rnd` runtime
///   function.
/// - Side effects: Updates @ref curLoc so the runtime call inherits the
///   builtin's source location.
Lowerer::RVal Lowerer::lowerRnd(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_rnd", {});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the LEN builtin.
/// @param c Builtin call expression representing `LEN`.
/// @return Integer length of the supplied string.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Issues a call to `rt_len` returning an `i64` result.
/// - Side effects: Updates @ref curLoc before emitting the runtime call.
Lowerer::RVal Lowerer::lowerLen(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the MID$ builtin with optional length argument.
/// @param c Builtin call expression representing `MID$`.
/// @return String slice materialized by runtime helpers.
/// @details
/// - Control flow: Straight-line emission while optionally branching on the
///   presence of the third argument at compile time.
/// - Emitted IL: Computes zero-based offsets, then calls either `rt_mid2` or
///   `rt_mid3`, marking which runtime entry points are required.
/// - Side effects: Updates @ref curLoc and records the runtime helper variant
///   needed for later linkage.
Lowerer::RVal Lowerer::lowerMid(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal i = ensureI64(lowerArg(c, 1), c.loc);
    Value start0 = emitBinary(Opcode::Add, Type(Type::Kind::I64), i.value, Value::constInt(-1));
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[2])
    {
        RVal n = ensureI64(lowerArg(c, 2), c.loc);
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid3", {s.value, start0, n.value});
        requestHelper(RuntimeFeature::Mid3);
        return {res, Type(Type::Kind::Str)};
    }
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid2", {s.value, start0});
    requestHelper(RuntimeFeature::Mid2);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the LEFT$ builtin.
/// @param c Builtin call expression representing `LEFT$`.
/// @return String value returned from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the length argument is an `i64` and calls
///   `rt_left`, tracking that the runtime stub is required.
/// - Side effects: Updates @ref curLoc and records the LEFT$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerLeft(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_left", {s.value, n.value});
    requestHelper(RuntimeFeature::Left);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the RIGHT$ builtin.
/// @param c Builtin call expression representing `RIGHT$`.
/// @return String slice produced by the runtime helper.
/// @details
/// - Control flow: Remains in the current block without branching.
/// - Emitted IL: Converts the count argument to `i64` and calls `rt_right`.
/// - Side effects: Updates @ref curLoc and records the RIGHT$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerRight(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_right", {s.value, n.value});
    requestHelper(RuntimeFeature::Right);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the STR$ builtin converting numbers to strings.
/// @param c Builtin call expression representing `STR$`.
/// @return String value returned by the runtime conversion helper.
/// @details
/// - Control flow: Straight-line emission that normalizes the operand type.
/// - Emitted IL: Delegates to @ref ensureF64 or @ref ensureI64 before calling
///   the appropriate runtime converter.
/// - Side effects: Updates @ref curLoc and mutates the operand's
///   @ref Lowerer::RVal to reflect any type promotion performed.
Lowerer::RVal Lowerer::lowerStr(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_f64_to_str", {v.value});
        return {res, Type(Type::Kind::Str)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_int_to_str", {v.value});
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the VAL builtin converting strings to integers.
/// @param c Builtin call expression representing `VAL`.
/// @return Integer value parsed by the runtime.
/// @details
/// - Control flow: Straight-line emission using the current block.
/// - Emitted IL: Calls `rt_to_int` returning an `i64` result.
/// - Side effects: Updates @ref curLoc before invoking the runtime routine.
Lowerer::RVal Lowerer::lowerVal(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {s.value});
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the INT builtin performing truncation toward zero.
/// @param c Builtin call expression representing `INT`.
/// @return Integer value after truncating the operand.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the operand is `f64` and converts via `fptosi`.
/// - Side effects: Updates @ref curLoc for the emitted conversion.
Lowerer::RVal Lowerer::lowerInt(const BuiltinCallExpr &c)
{
    RVal f = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), f.value);
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the INSTR builtin for substring search.
/// @param c Builtin call expression representing `INSTR`.
/// @return Integer index result provided by the runtime.
/// @details
/// - Control flow: Linear emission that chooses between the two-argument and
///   three-argument runtime entry points based on AST structure.
/// - Emitted IL: Adjusts user-facing 1-based indices, then calls either
///   `rt_instr2` or `rt_instr3` and records which helper is needed.
/// - Side effects: Updates @ref curLoc and records which INSTR helper variant
///   is required for linkage.
Lowerer::RVal Lowerer::lowerInstr(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[0])
    {
        RVal start = ensureI64(lowerArg(c, 0), c.loc);
        Value start0 =
            emitBinary(Opcode::Add, Type(Type::Kind::I64), start.value, Value::constInt(-1));
        RVal hay = lowerArg(c, 1);
        RVal needle = lowerArg(c, 2);
        Value res =
            emitCallRet(Type(Type::Kind::I64), "rt_instr3", {start0, hay.value, needle.value});
        requestHelper(RuntimeFeature::Instr3);
        return {res, Type(Type::Kind::I64)};
    }
    RVal hay = lowerArg(c, 0);
    RVal needle = lowerArg(c, 1);
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_instr2", {hay.value, needle.value});
    requestHelper(RuntimeFeature::Instr2);
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the LTRIM$ builtin.
/// @param c Builtin call expression representing `LTRIM$`.
/// @return Trimmed string value from the runtime helper.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Calls `rt_ltrim` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the LTRIM$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerLtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ltrim", {s.value});
    requestHelper(RuntimeFeature::Ltrim);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the RTRIM$ builtin.
/// @param c Builtin call expression representing `RTRIM$`.
/// @return Trimmed string value from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Calls `rt_rtrim` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the RTRIM$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerRtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_rtrim", {s.value});
    requestHelper(RuntimeFeature::Rtrim);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the TRIM$ builtin.
/// @param c Builtin call expression representing `TRIM$`.
/// @return Trimmed string value from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Calls `rt_trim` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the TRIM$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerTrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_trim", {s.value});
    requestHelper(RuntimeFeature::Trim);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the UCASE$ builtin.
/// @param c Builtin call expression representing `UCASE$`.
/// @return Upper-cased string produced by the runtime helper.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Calls `rt_ucase` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the UCASE$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerUcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ucase", {s.value});
    requestHelper(RuntimeFeature::Ucase);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the LCASE$ builtin.
/// @param c Builtin call expression representing `LCASE$`.
/// @return Lower-cased string produced by the runtime helper.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Calls `rt_lcase` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the LCASE$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerLcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_lcase", {s.value});
    requestHelper(RuntimeFeature::Lcase);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the CHR$ builtin.
/// @param c Builtin call expression representing `CHR$`.
/// @return Single-character string produced by the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Converts the code point to `i64` and calls `rt_chr`.
/// - Side effects: Updates @ref curLoc and records the CHR$ runtime helper requirement.
Lowerer::RVal Lowerer::lowerChr(const BuiltinCallExpr &c)
{
    RVal code = ensureI64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_chr", {code.value});
    requestHelper(RuntimeFeature::Chr);
    return {res, Type(Type::Kind::Str)};
}

/// @brief Lower the ASC builtin.
/// @param c Builtin call expression representing `ASC`.
/// @return Integer code point extracted by the runtime helper.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Calls `rt_asc` with the lowered string argument.
/// - Side effects: Updates @ref curLoc and records the ASC runtime helper requirement.
Lowerer::RVal Lowerer::lowerAsc(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_asc", {s.value});
    requestHelper(RuntimeFeature::Asc);
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the SQR builtin (square root).
/// @param c Builtin call expression representing `SQR`.
/// @return Floating-point result produced by the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Normalizes the operand to `f64` and calls `rt_sqrt`.
/// - Side effects: Updates @ref curLoc prior to the runtime call.
Lowerer::RVal Lowerer::lowerSqr(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sqrt", {v.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the ABS builtin.
/// @param c Builtin call expression representing `ABS`.
/// @return Absolute value result with the matching numeric type.
/// @details
/// - Control flow: Straight-line within the current block.
/// - Emitted IL: Chooses between `rt_abs_f64` and `rt_abs_i64` after ensuring
///   the operand has the appropriate type.
/// - Side effects: Updates @ref curLoc and mutates the operand
///   @ref Lowerer::RVal when conversions are performed.
Lowerer::RVal Lowerer::lowerAbs(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::F64), "rt_abs_f64", {v.value});
        return {res, Type(Type::Kind::F64)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_abs_i64", {v.value});
    return {res, Type(Type::Kind::I64)};
}

/// @brief Lower the FLOOR builtin.
/// @param c Builtin call expression representing `FLOOR`.
/// @return Floating-point result from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the operand is `f64` and calls `rt_floor`.
/// - Side effects: Updates @ref curLoc prior to emitting the call.
Lowerer::RVal Lowerer::lowerFloor(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_floor", {v.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the CEIL builtin.
/// @param c Builtin call expression representing `CEIL`.
/// @return Floating-point result from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the operand is `f64` and calls `rt_ceil`.
/// - Side effects: Updates @ref curLoc prior to emitting the call.
Lowerer::RVal Lowerer::lowerCeil(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_ceil", {v.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the SIN builtin.
/// @param c Builtin call expression representing `SIN`.
/// @return Floating-point result from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the operand is `f64` and calls `rt_sin`.
/// - Side effects: Updates @ref curLoc prior to emitting the call.
Lowerer::RVal Lowerer::lowerSin(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sin", {v.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the COS builtin.
/// @param c Builtin call expression representing `COS`.
/// @return Floating-point result from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures the operand is `f64` and calls `rt_cos`.
/// - Side effects: Updates @ref curLoc prior to emitting the call.
Lowerer::RVal Lowerer::lowerCos(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_cos", {v.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Lower the POW builtin.
/// @param c Builtin call expression representing `POW`.
/// @return Floating-point result from the runtime helper.
/// @details
/// - Control flow: Linear within the current block.
/// - Emitted IL: Ensures both operands are `f64` and calls `rt_pow`.
/// - Side effects: Updates @ref curLoc prior to emitting the call.
Lowerer::RVal Lowerer::lowerPow(const BuiltinCallExpr &c)
{
    RVal a = ensureF64(lowerArg(c, 0), c.loc);
    RVal b = ensureF64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_pow", {a.value, b.value});
    return {res, Type(Type::Kind::F64)};
}

/// @brief Dispatch lowering for builtin call expressions.
/// @param c Builtin call AST node.
/// @return Lowered value and type produced by the builtin implementation.
/// @details
/// - Control flow: Delegates to the registered lowering member function when
///   available, otherwise falls back to an integer zero constant.
/// - Emitted IL: Dependent on the selected builtin handler.
/// - Side effects: None beyond those performed by the dispatched helper.
Lowerer::RVal Lowerer::lowerBuiltinCall(const BuiltinCallExpr &c)
{
    const auto &info = getBuiltinInfo(c.builtin);
    if (info.lower)
        return (this->*(info.lower))(c);
    return {Value::constInt(0), Type(Type::Kind::I64)};
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

