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

    // Handle optional nil comparisons specially
    // x = nil or x <> nil where x is a value-type optional
    if (expr.op == BinaryExpr::Op::Eq || expr.op == BinaryExpr::Op::Ne)
    {
        bool leftIsNil = (expr.left->kind == ExprKind::NilLiteral);
        bool rightIsNil = (expr.right->kind == ExprKind::NilLiteral);

        if (leftIsNil || rightIsNil)
        {
            // Get the non-nil side
            const Expr *optExpr = leftIsNil ? expr.right.get() : expr.left.get();
            PasType optType = typeOfExpr(*optExpr);

            if (optType.isValueTypeOptional() && optExpr->kind == ExprKind::Name)
            {
                // Get the slot for the value-type optional
                const auto &nameExpr = static_cast<const NameExpr &>(*optExpr);
                std::string key = nameExpr.name;
                for (auto &c : key)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                auto it = locals_.find(key);
                if (it != locals_.end())
                {
                    Value optSlot = it->second;
                    // Check hasValue flag
                    Value hasValue = emitOptionalHasValue(optSlot, optType);

                    // For x = nil: return !hasValue (true if no value)
                    // For x <> nil: return hasValue (true if has value)
                    if (expr.op == BinaryExpr::Op::Eq)
                    {
                        // x = nil means hasValue is false
                        Value result = emitBinary(Opcode::ICmpEq,
                                                  Type(Type::Kind::I1),
                                                  hasValue,
                                                  Value::constBool(false));
                        return {result, Type(Type::Kind::I1)};
                    }
                    else
                    {
                        // x <> nil means hasValue is true
                        return {hasValue, Type(Type::Kind::I1)};
                    }
                }
            }
        }
    }

    // Lower operands
    LowerResult lhs = lowerExpr(*expr.left);
    LowerResult rhs = lowerExpr(*expr.right);

    // Handle pointer comparisons (ptr vs ptr or ptr vs nil)
    // Pointers are compared using UCmpEq/UCmpNe which treats them as unsigned i64 values
    bool isPointer = (lhs.type.kind == Type::Kind::Ptr || rhs.type.kind == Type::Kind::Ptr);
    if (isPointer)
    {
        // For pointer comparison, use PtrCmpEq/PtrCmpNe opcodes
        // Since those don't exist, we use a store-load trick to convert ptr to i64
        // or simply use the Value directly since ICmpEq/ICmpNe can handle ptr values
        // when both operands are consistent.
        //
        // Actually, the simpler approach: use Value::constInt(0) for null comparisons
        // and let the verifier/VM handle ptr vs ptr comparisons.
        Value lhsVal = lhs.value;
        Value rhsVal = rhs.value;

        // If comparing to null, replace null with constInt(0) which is semantically equivalent
        // This avoids type mismatch since both will be treated as integers by ICmpEq/ICmpNe
        if (lhs.value.kind == Value::Kind::NullPtr)
            lhsVal = Value::constInt(0);
        if (rhs.value.kind == Value::Kind::NullPtr)
            rhsVal = Value::constInt(0);

        // For ptr-to-ptr comparison, we need to convert to i64
        // Since there's no ptrtoint opcode, use a stack slot
        auto ptrToInt = [&](Value ptrVal) -> Value
        {
            Value slot = emitAlloca(8); // ptr is 8 bytes
            emitStore(Type(Type::Kind::Ptr), slot, ptrVal);
            return emitLoad(Type(Type::Kind::I64), slot);
        };

        if (lhs.type.kind == Type::Kind::Ptr && lhs.value.kind != Value::Kind::NullPtr)
            lhsVal = ptrToInt(lhs.value);
        if (rhs.type.kind == Type::Kind::Ptr && rhs.value.kind != Value::Kind::NullPtr)
            rhsVal = ptrToInt(rhs.value);

        switch (expr.op)
        {
            case BinaryExpr::Op::Eq:
                return {emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), lhsVal, rhsVal),
                        Type(Type::Kind::I1)};
            case BinaryExpr::Op::Ne:
                return {emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), lhsVal, rhsVal),
                        Type(Type::Kind::I1)};
            default:
                // Other comparisons not supported for pointers
                break;
        }
    }

    // Handle string comparisons via runtime call
    bool isString = (lhs.type.kind == Type::Kind::Str || rhs.type.kind == Type::Kind::Str);
    if (isString)
    {
        const char *rtFunc = nullptr;
        bool negate = false;

        switch (expr.op)
        {
            case BinaryExpr::Op::Eq:
                rtFunc = "rt_str_eq";
                break;
            case BinaryExpr::Op::Ne:
                rtFunc = "rt_str_eq";
                negate = true;
                break;
            case BinaryExpr::Op::Lt:
                rtFunc = "rt_str_lt";
                break;
            case BinaryExpr::Op::Le:
                rtFunc = "rt_str_le";
                break;
            case BinaryExpr::Op::Gt:
                rtFunc = "rt_str_gt";
                break;
            case BinaryExpr::Op::Ge:
                rtFunc = "rt_str_ge";
                break;
            default:
                break;
        }

        if (rtFunc)
        {
            usedExterns_.insert(rtFunc);
            Value result = emitCallRet(Type(Type::Kind::I1), rtFunc, {lhs.value, rhs.value});
            if (negate)
            {
                // Negate the result for !=
                Value zero = Value::constBool(false);
                result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), result, zero);
            }
            return {result, Type(Type::Kind::I1)};
        }
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

    // Get the type of the left operand (the optional)
    PasType leftType = typeOfExpr(*expr.left);

    // Evaluate left operand - for value-type optionals, this returns the slot address
    // For reference-type optionals, lowerExpr returns the loaded pointer value
    LowerResult left = lowerExpr(*expr.left);

    // Determine result type from the unwrapped optional or right side
    Type resultType = leftType.innerType ? mapType(*leftType.innerType) : left.type;

    // Allocate result slot before any branches
    int64_t resultSize = leftType.innerType ? sizeOf(*leftType.innerType) : 8;
    Value resultSlot = emitAlloca(resultSize);

    // Check if optional has a value using type-aware nil check
    Value isNotNil;
    if (leftType.isValueTypeOptional())
    {
        // For value-type optionals, left.value is the address of the optional slot
        // We need to get the slot address - if lowerExpr returned a loaded value, we have a problem
        // Actually, for optional expressions, we need the slot to check hasValue
        // Let's handle this by getting the slot from locals if it's a name expression
        if (expr.left->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*expr.left);
            std::string key = nameExpr.name;
            for (auto &c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value optSlot = it->second;
                isNotNil = emitOptionalHasValue(optSlot, leftType);

                emitCBr(isNotNil, useLeftBlock, evalRhsBlock);

                // Use left value (not nil) - load the unwrapped value
                setBlock(useLeftBlock);
                Value leftVal = emitOptionalLoadValue(optSlot, leftType);
                emitStore(resultType, resultSlot, leftVal);
                emitBr(joinBlock);

                // Evaluate right operand (left was nil) - store to result slot
                setBlock(evalRhsBlock);
                LowerResult right = lowerExpr(*expr.right);
                emitStore(resultType, resultSlot, right.value);
                emitBr(joinBlock);

                // Join block - load from result slot
                setBlock(joinBlock);
                Value result = emitLoad(resultType, resultSlot);

                return {result, resultType};
            }
        }
        // Fallback: treat as reference type (shouldn't happen with proper typing)
        isNotNil = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), left.value, Value::null());
    }
    else
    {
        // Reference-type optional: check for non-null pointer
        isNotNil = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), left.value, Value::null());
    }

    emitCBr(isNotNil, useLeftBlock, evalRhsBlock);

    // Use left value (not nil) - store to result slot
    setBlock(useLeftBlock);
    emitStore(resultType, resultSlot, left.value);
    emitBr(joinBlock);

    // Evaluate right operand (left was nil) - store to result slot
    setBlock(evalRhsBlock);
    LowerResult right = lowerExpr(*expr.right);
    emitStore(resultType, resultSlot, right.value);
    emitBr(joinBlock);

    // Join block - load from result slot
    setBlock(joinBlock);
    Value result = emitLoad(resultType, resultSlot);

    return {result, resultType};
}

} // namespace il::frontends::pascal
