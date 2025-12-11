//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr_Ops.cpp
// Purpose: Unary and binary expression lowering for Pascal AST to IL.
// Key invariants: Proper short-circuit evaluation for And/Or/Coalesce.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"

namespace il::frontends::pascal
{

LowerResult Lowerer::lowerUnary(const UnaryExpr &expr)
{
    LowerResult operand = lowerExpr(*expr.operand);

    switch (expr.op)
    {
        case UnaryExpr::Op::Neg:
            if (operand.type.kind == Type::Kind::F64)
            {
                // Negate float: 0.0 - x
                Value zero = Value::constFloat(0.0);
                Value result = emitBinary(Opcode::FSub, operand.type, zero, operand.value);
                return {result, operand.type};
            }
            else
            {
                // Negate integer: 0 - x (use overflow-checking subtraction)
                Value zero = Value::constInt(0);
                Value result =
                    emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), zero, operand.value);
                return {result, Type(Type::Kind::I64)};
            }

        case UnaryExpr::Op::Not:
            // Boolean not
            {
                // If operand is i1, first zext to i64
                Value opVal = operand.value;
                if (operand.type.kind == Type::Kind::I1)
                {
                    opVal = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), opVal);
                }
                // xor with 1
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::Xor, Type(Type::Kind::I64), opVal, one);
                // Truncate back to i1
                result = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), result);
                return {result, Type(Type::Kind::I1)};
            }

        case UnaryExpr::Op::Plus:
            // Identity
            return operand;
    }

    return operand;
}

LowerResult Lowerer::lowerBinary(const BinaryExpr &expr)
{
    // Handle short-circuit operators specially
    switch (expr.op)
    {
        case BinaryExpr::Op::And:
            return lowerLogicalAnd(expr);
        case BinaryExpr::Op::Or:
            return lowerLogicalOr(expr);
        case BinaryExpr::Op::Coalesce:
            return lowerCoalesce(expr);
        default:
            break;
    }

    // Lower operands
    LowerResult lhs = lowerExpr(*expr.left);
    LowerResult rhs = lowerExpr(*expr.right);

    // Handle string comparisons via runtime call
    bool isString = (lhs.type.kind == Type::Kind::Str || rhs.type.kind == Type::Kind::Str);
    if (isString && (expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne))
    {
        // Call rt_str_eq(str, str) -> i1
        usedExterns_.insert("rt_str_eq");
        Value result = emitCallRet(Type(Type::Kind::I1), "rt_str_eq", {lhs.value, rhs.value});
        if (expr.op == BinaryExpr::Op::Ne)
        {
            // Negate the result for !=
            Value zero = Value::constBool(false);
            result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), result, zero);
        }
        return {result, Type(Type::Kind::I1)};
    }

    // Determine result type
    bool isFloat = (lhs.type.kind == Type::Kind::F64 || rhs.type.kind == Type::Kind::F64);

    // Promote integer to float if needed
    Value lhsVal = lhs.value;
    Value rhsVal = rhs.value;
    if (isFloat)
    {
        if (lhs.type.kind != Type::Kind::F64)
            lhsVal = emitSitofp(lhs.value);
        if (rhs.type.kind != Type::Kind::F64)
            rhsVal = emitSitofp(rhs.value);
    }

    Type resultType = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);

    switch (expr.op)
    {
        // Arithmetic
        // For signed integers (Pascal Integer type is always signed), use the .ovf
        // variants which trap on overflow as required by the IL spec
        case BinaryExpr::Op::Add:
            return {
                emitBinary(isFloat ? Opcode::FAdd : Opcode::IAddOvf, resultType, lhsVal, rhsVal),
                resultType};

        case BinaryExpr::Op::Sub:
            return {
                emitBinary(isFloat ? Opcode::FSub : Opcode::ISubOvf, resultType, lhsVal, rhsVal),
                resultType};

        case BinaryExpr::Op::Mul:
            return {
                emitBinary(isFloat ? Opcode::FMul : Opcode::IMulOvf, resultType, lhsVal, rhsVal),
                resultType};

        case BinaryExpr::Op::Div:
            // Real division always returns Real
            if (!isFloat)
            {
                lhsVal = emitSitofp(lhs.value);
                rhsVal = emitSitofp(rhs.value);
            }
            return {emitBinary(Opcode::FDiv, Type(Type::Kind::F64), lhsVal, rhsVal),
                    Type(Type::Kind::F64)};

        case BinaryExpr::Op::IntDiv:
            // Integer division (with trap on divide-by-zero)
            return {emitBinary(Opcode::SDivChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                    Type(Type::Kind::I64)};

        case BinaryExpr::Op::Mod:
            // Integer remainder (with trap on divide-by-zero)
            return {emitBinary(Opcode::SRemChk0, Type(Type::Kind::I64), lhs.value, rhs.value),
                    Type(Type::Kind::I64)};

        // Comparisons
        case BinaryExpr::Op::Eq:
            return {emitBinary(isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        case BinaryExpr::Op::Ne:
            return {emitBinary(isFloat ? Opcode::FCmpNE : Opcode::ICmpNe,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        case BinaryExpr::Op::Lt:
            return {emitBinary(isFloat ? Opcode::FCmpLT : Opcode::SCmpLT,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        case BinaryExpr::Op::Le:
            return {emitBinary(isFloat ? Opcode::FCmpLE : Opcode::SCmpLE,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        case BinaryExpr::Op::Gt:
            return {emitBinary(isFloat ? Opcode::FCmpGT : Opcode::SCmpGT,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        case BinaryExpr::Op::Ge:
            return {emitBinary(isFloat ? Opcode::FCmpGE : Opcode::SCmpGE,
                               Type(Type::Kind::I1),
                               lhsVal,
                               rhsVal),
                    Type(Type::Kind::I1)};

        default:
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerLogicalAnd(const BinaryExpr &expr)
{
    // Short-circuit: if left is false, result is false; else result is right
    size_t evalRhsBlock = createBlock("and_rhs");
    size_t shortCircuitBlock = createBlock("and_short");
    size_t joinBlock = createBlock("and_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, evaluate right; else short-circuit with false
    emitCBr(leftBool, evalRhsBlock, shortCircuitBlock);

    // Short-circuit: left was false, result is false
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(0));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerLogicalOr(const BinaryExpr &expr)
{
    // Short-circuit: if left is true, result is true; else result is right
    size_t shortCircuitBlock = createBlock("or_short");
    size_t evalRhsBlock = createBlock("or_rhs");
    size_t joinBlock = createBlock("or_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(1);

    // Evaluate left
    LowerResult left = lowerExpr(*expr.left);
    Value leftBool = left.value;

    // If left is true, short-circuit with true; else evaluate right
    emitCBr(leftBool, shortCircuitBlock, evalRhsBlock);

    // Short-circuit: left was true, result is true
    setBlock(shortCircuitBlock);
    emitStore(Type(Type::Kind::I1), resultSlot, Value::constInt(1));
    emitBr(joinBlock);

    // Evaluate right in evalRhsBlock
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(Type(Type::Kind::I1), resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load result
    setBlock(joinBlock);
    Value result = emitLoad(Type(Type::Kind::I1), resultSlot);

    return {result, Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerCoalesce(const BinaryExpr &expr)
{
    // a ?? b: if a is not nil, use a; else evaluate and use b
    // Short-circuits: b is only evaluated if a is nil

    size_t useLeftBlock = createBlock("coalesce_use_lhs");
    size_t evalRhsBlock = createBlock("coalesce_rhs");
    size_t joinBlock = createBlock("coalesce_join");

    // Allocate result slot before any branches
    Value resultSlot = emitAlloca(8);

    // Evaluate left operand
    LowerResult left = lowerExpr(*expr.left);

    // For reference-type optionals, null pointer means nil
    // For value-type optionals, we'd check the hasValue flag at offset 0
    // Currently, both are represented as Ptr (null = nil)
    Value isNotNil = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), left.value, Value::null());

    emitCBr(isNotNil, useLeftBlock, evalRhsBlock);

    // Use left value (not nil) - store to result slot
    setBlock(useLeftBlock);
    emitStore(left.type, resultSlot, left.value);
    emitBr(joinBlock);

    // Evaluate right operand (left was nil) - store to result slot
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(right.type, resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load from result slot
    setBlock(joinBlock);
    Value result = emitLoad(right.type, resultSlot);

    return {result, right.type};
}

} // namespace il::frontends::pascal
