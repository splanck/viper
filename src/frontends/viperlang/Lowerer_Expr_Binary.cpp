//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Binary.cpp
/// @brief Binary and unary expression lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
{

using namespace runtime;
using il::core::Value;
using il::core::Type;
using il::core::Opcode;

//=============================================================================
// Helper Functions
//=============================================================================

Value Lowerer::wrapValueForOptionalField(Value val, TypeRef fieldType, TypeRef valueType)
{
    if (!fieldType || fieldType->kind != TypeKindSem::Optional)
        return val;

    TypeRef innerType = fieldType->innerType();
    if (valueType && valueType->kind == TypeKindSem::Optional)
    {
        return val; // Already optional
    }
    else if (valueType && valueType->kind == TypeKindSem::Unit)
    {
        return Value::null();
    }
    else if (innerType)
    {
        return emitOptionalWrap(val, innerType);
    }
    return val;
}

Value Lowerer::extendOperandForComparison(Value val, Type type)
{
    if (val.kind == Value::Kind::NullPtr)
    {
        return Value::constInt(0);
    }
    else if (type.kind == Type::Kind::I1)
    {
        return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), val);
    }
    else if (type.kind == Type::Kind::Ptr)
    {
        // Convert pointer to i64 via alloca/store/load
        unsigned slotId = nextTempId();
        il::core::Instr slotInstr;
        slotInstr.result = slotId;
        slotInstr.op = Opcode::Alloca;
        slotInstr.type = Type(Type::Kind::Ptr);
        slotInstr.operands = {Value::constInt(8)};
        blockMgr_.currentBlock()->instructions.push_back(slotInstr);
        Value slot = Value::temp(slotId);
        emitStore(slot, val, Type(Type::Kind::Ptr));
        return emitLoad(slot, Type(Type::Kind::I64));
    }
    return val;
}

//=============================================================================
// Binary Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerBinary(BinaryExpr *expr)
{
    // Handle assignment specially
    if (expr->op == BinaryOp::Assign)
    {
        auto right = lowerExpr(expr->right.get());
        TypeRef rightType = sema_.typeOf(expr->right.get());

        // LHS must be an identifier, index, or field expression
        if (auto *ident = dynamic_cast<IdentExpr *>(expr->left.get()))
        {
            TypeRef targetType = nullptr;
            auto typeIt = localTypes_.find(ident->name);
            if (typeIt != localTypes_.end())
                targetType = typeIt->second;
            else
                targetType = sema_.typeOf(expr->left.get());

            Value assignValue = wrapValueForOptionalField(right.value, targetType, rightType);
            Type assignType = (targetType && targetType->kind == TypeKindSem::Optional)
                              ? Type(Type::Kind::Ptr) : right.type;

            // Check if this is a slot-based variable
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                storeToSlot(ident->name, assignValue, assignType);
                return right;
            }

            // Check for implicit field assignment inside a value type method
            if (currentValueType_)
            {
                const FieldLayout *field = currentValueType_->findField(ident->name);
                if (field)
                {
                    Value selfPtr;
                    if (getSelfPtr(selfPtr))
                    {
                        Value fieldValue = wrapValueForOptionalField(right.value, field->type, rightType);
                        emitFieldStore(field, selfPtr, fieldValue);
                        return right;
                    }
                }
            }

            // Check for implicit field assignment inside an entity method
            if (currentEntityType_)
            {
                const FieldLayout *field = currentEntityType_->findField(ident->name);
                if (field)
                {
                    Value selfPtr;
                    if (getSelfPtr(selfPtr))
                    {
                        Value fieldValue = wrapValueForOptionalField(right.value, field->type, rightType);
                        emitFieldStore(field, selfPtr, fieldValue);
                        return right;
                    }
                }
            }

            // Check for global variable assignment
            auto globalIt = globalVariables_.find(ident->name);
            if (globalIt != globalVariables_.end())
            {
                TypeRef globalType = globalIt->second;
                Type ilType = mapType(globalType);
                Value addr = getGlobalVarAddr(ident->name, globalType);
                Value storeValue = wrapValueForOptionalField(assignValue, globalType, rightType);
                emitStore(addr, storeValue, ilType);
                return right;
            }

            // Regular variable assignment
            defineLocal(ident->name, assignValue);
            if (targetType)
                localTypes_[ident->name] = targetType;
            return right;
        }

        // Handle index assignment (list[i] = value, map[key] = value)
        if (auto *indexExpr = dynamic_cast<IndexExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(indexExpr->base.get());
            auto index = lowerExpr(indexExpr->index.get());
            TypeRef baseType = sema_.typeOf(indexExpr->base.get());

            Value boxedValue = emitBox(right.value, right.type);
            if (baseType && baseType->kind == TypeKindSem::Map)
                emitCall(kMapSet, {base.value, index.value, boxedValue});
            else
                emitCall(kListSet, {base.value, index.value, boxedValue});
            return right;
        }

        // Handle field assignment (obj.field = value)
        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(fieldExpr->base.get());
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());

            // Unwrap Optional types for field assignment
            // This handles variables assigned from optionals after null checks
            // (e.g., `var row = maybeRow;` where maybeRow is Row?)
            if (baseType && baseType->kind == TypeKindSem::Optional && baseType->innerType())
            {
                baseType = baseType->innerType();
            }

            if (baseType)
            {
                std::string typeName = baseType->name;

                // Check value types
                auto valueIt = valueTypes_.find(typeName);
                if (valueIt != valueTypes_.end())
                {
                    const FieldLayout *field = valueIt->second.findField(fieldExpr->field);
                    if (field)
                    {
                        Value fieldValue = wrapValueForOptionalField(right.value, field->type, rightType);
                        emitFieldStore(field, base.value, fieldValue);
                        return right;
                    }
                }

                // Check entity types
                auto entityIt = entityTypes_.find(typeName);
                if (entityIt != entityTypes_.end())
                {
                    const FieldLayout *field = entityIt->second.findField(fieldExpr->field);
                    if (field)
                    {
                        Value fieldValue = wrapValueForOptionalField(right.value, field->type, rightType);
                        emitFieldStore(field, base.value, fieldValue);
                        return right;
                    }
                }
            }
        }

        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Non-assignment binary operations
    auto left = lowerExpr(expr->left.get());
    auto right = lowerExpr(expr->right.get());

    TypeRef leftType = sema_.typeOf(expr->left.get());
    TypeRef rightType = sema_.typeOf(expr->right.get());

    bool leftIsFloat = leftType && leftType->kind == TypeKindSem::Number;
    bool rightIsFloat = rightType && rightType->kind == TypeKindSem::Number;
    bool isFloat = leftIsFloat || rightIsFloat;

    // Handle mixed-type arithmetic: promote integer operand to float
    if (isFloat && !leftIsFloat && leftType && leftType->isIntegral())
    {
        unsigned convId = nextTempId();
        il::core::Instr convInstr;
        convInstr.result = convId;
        convInstr.op = Opcode::Sitofp;
        convInstr.type = Type(Type::Kind::F64);
        convInstr.operands = {left.value};
        blockMgr_.currentBlock()->instructions.push_back(convInstr);
        left.value = Value::temp(convId);
        left.type = Type(Type::Kind::F64);
    }
    else if (isFloat && !rightIsFloat && rightType && rightType->isIntegral())
    {
        unsigned convId = nextTempId();
        il::core::Instr convInstr;
        convInstr.result = convId;
        convInstr.op = Opcode::Sitofp;
        convInstr.type = Type(Type::Kind::F64);
        convInstr.operands = {right.value};
        blockMgr_.currentBlock()->instructions.push_back(convInstr);
        right.value = Value::temp(convId);
        right.type = Type(Type::Kind::F64);
    }

    Opcode op;
    Type resultType = isFloat ? Type(Type::Kind::F64) : left.type;

    switch (expr->op)
    {
        case BinaryOp::Add:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String concatenation
                Value rightStr = right.value;
                if (rightType && rightType->kind == TypeKindSem::Integer)
                    rightStr = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {right.value});
                else if (rightType && rightType->kind == TypeKindSem::Number)
                    rightStr = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {right.value});
                else if (rightType && rightType->kind == TypeKindSem::Boolean)
                    rightStr = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {right.value});

                Value result = emitCallRet(Type(Type::Kind::Str), kStringConcat, {left.value, rightStr});
                return {result, Type(Type::Kind::Str)};
            }
            op = isFloat ? Opcode::FAdd : (options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add);
            break;

        case BinaryOp::Sub:
            op = isFloat ? Opcode::FSub : (options_.overflowChecks ? Opcode::ISubOvf : Opcode::Sub);
            break;

        case BinaryOp::Mul:
            op = isFloat ? Opcode::FMul : (options_.overflowChecks ? Opcode::IMulOvf : Opcode::Mul);
            break;

        case BinaryOp::Div:
            op = isFloat ? Opcode::FDiv : (options_.overflowChecks ? Opcode::SDivChk0 : Opcode::SDiv);
            break;

        case BinaryOp::Mod:
            op = options_.overflowChecks ? Opcode::SRemChk0 : Opcode::SRem;
            break;

        case BinaryOp::Eq:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String equality: rt_str_eq returns i64 (0 or 1), convert to boolean
                Value result = emitCallRet(Type(Type::Kind::I64), kStringEquals, {left.value, right.value});
                Value boolResult = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            if (isFloat)
            {
                op = Opcode::FCmpEQ;
                resultType = Type(Type::Kind::I1);
            }
            else
            {
                Value lhsExt = extendOperandForComparison(left.value, left.type);
                Value rhsExt = extendOperandForComparison(right.value, right.type);
                Value result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), lhsExt, rhsExt);
                return {result, Type(Type::Kind::I1)};
            }
            break;

        case BinaryOp::Ne:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String inequality: rt_str_eq returns i64 (0 or 1), invert for !=
                Value eqResult = emitCallRet(Type(Type::Kind::I64), kStringEquals, {left.value, right.value});
                Value result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), eqResult, Value::constInt(0));
                return {result, Type(Type::Kind::I1)};
            }
            if (isFloat)
            {
                op = Opcode::FCmpNE;
                resultType = Type(Type::Kind::I1);
            }
            else
            {
                Value lhsExt = extendOperandForComparison(left.value, left.type);
                Value rhsExt = extendOperandForComparison(right.value, right.type);
                Value result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), lhsExt, rhsExt);
                return {result, Type(Type::Kind::I1)};
            }
            break;

        case BinaryOp::Lt:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for <
                Value result = emitCallRet(Type(Type::Kind::I64), "rt_str_lt", {left.value, right.value});
                Value boolResult = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Le:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for <=
                Value result = emitCallRet(Type(Type::Kind::I64), "rt_str_le", {left.value, right.value});
                Value boolResult = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Gt:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for >
                Value result = emitCallRet(Type(Type::Kind::I64), "rt_str_gt", {left.value, right.value});
                Value boolResult = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Ge:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for >=
                Value result = emitCallRet(Type(Type::Kind::I64), "rt_str_ge", {left.value, right.value});
                Value boolResult = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::And:
        {
            Value lhsExt = (left.type.kind == Type::Kind::I1)
                           ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value) : left.value;
            Value rhsExt = (right.type.kind == Type::Kind::I1)
                           ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value) : right.value;
            Value andResult = emitBinary(Opcode::And, Type(Type::Kind::I64), lhsExt, rhsExt);
            Value truncResult = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), andResult);
            return {truncResult, Type(Type::Kind::I1)};
        }

        case BinaryOp::Or:
        {
            Value lhsExt = (left.type.kind == Type::Kind::I1)
                           ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value) : left.value;
            Value rhsExt = (right.type.kind == Type::Kind::I1)
                           ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value) : right.value;
            Value orResult = emitBinary(Opcode::Or, Type(Type::Kind::I64), lhsExt, rhsExt);
            Value truncResult = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), orResult);
            return {truncResult, Type(Type::Kind::I1)};
        }

        case BinaryOp::BitAnd:
            op = Opcode::And;
            break;

        case BinaryOp::BitOr:
            op = Opcode::Or;
            break;

        case BinaryOp::BitXor:
            op = Opcode::Xor;
            break;

        case BinaryOp::Assign:
            // Handled above
            break;
    }

    Value result = emitBinary(op, resultType, left.value, right.value);
    return {result, resultType};
}

//=============================================================================
// Unary Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerUnary(UnaryExpr *expr)
{
    auto operand = lowerExpr(expr->operand.get());
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    bool isFloat = operandType && operandType->kind == TypeKindSem::Number;

    switch (expr->op)
    {
        case UnaryOp::Neg:
        {
            if (isFloat)
            {
                Value result = emitBinary(Opcode::FSub, operand.type, Value::constFloat(0.0), operand.value);
                return {result, operand.type};
            }
            else
            {
                Opcode subOp = options_.overflowChecks ? Opcode::ISubOvf : Opcode::Sub;
                Value result = emitBinary(subOp, operand.type, Value::constInt(0), operand.value);
                return {result, operand.type};
            }
        }

        case UnaryOp::Not:
        {
            Value opVal = operand.value;
            if (operand.type.kind == Type::Kind::I1)
                opVal = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), operand.value);
            Value result = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), opVal, Value::constInt(0));
            return {result, Type(Type::Kind::I1)};
        }

        case UnaryOp::BitNot:
        {
            Value result = emitBinary(Opcode::Xor, operand.type, operand.value, Value::constInt(-1));
            return {result, operand.type};
        }
    }

    return operand;
}

} // namespace il::frontends::viperlang
