//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Binary.cpp
/// @brief Binary and unary expression lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia
{

using namespace runtime;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;

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
    else if (type.kind == Type::Kind::Ptr || type.kind == Type::Kind::Str)
    {
        // Convert pointer/string to i64 via alloca/store/load
        unsigned slotId = nextTempId();
        il::core::Instr slotInstr;
        slotInstr.result = slotId;
        slotInstr.op = Opcode::Alloca;
        slotInstr.type = Type(Type::Kind::Ptr);
        slotInstr.operands = {Value::constInt(8)};
        slotInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(slotInstr);
        Value slot = Value::temp(slotId);
        emitStore(slot, val, type);
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
                                  ? Type(Type::Kind::Ptr)
                                  : right.type;

            // Unbox obj (Ptr) to primitive type when assigning a boxed value to a typed slot.
            // This handles e.g. `intField = list.Get(i)` where List.Get() returns Ptr.
            if (right.type.kind == Type::Kind::Ptr && targetType)
            {
                Type targetILType = mapType(targetType);
                if (targetILType.kind != Type::Kind::Ptr)
                {
                    assignValue = emitUnbox(assignValue, targetILType).value;
                    assignType = targetILType;
                }
            }

            // Handle value type copy semantics - deep copy on assignment
            if (rightType && rightType->kind == TypeKindSem::Value)
            {
                const ValueTypeInfo *info = getOrCreateValueTypeInfo(rightType->name);
                if (info)
                {
                    assignValue = emitValueTypeCopy(*info, assignValue);
                }
            }

            // Check if this is a slot-based variable
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                storeToSlot(ident->name, assignValue, assignType);
                // The assigned value is consumed by the slot — don't release
                consumeDeferred(assignValue);
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
                        Value fieldValue =
                            wrapValueForOptionalField(right.value, field->type, rightType);
                        // Unbox obj (Ptr) to the field's primitive IL type.
                        if (right.type.kind == Type::Kind::Ptr && field->type)
                        {
                            Type fieldILType = mapType(field->type);
                            if (fieldILType.kind != Type::Kind::Ptr)
                                fieldValue = emitUnbox(fieldValue, fieldILType).value;
                        }
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
                        Value fieldValue =
                            wrapValueForOptionalField(right.value, field->type, rightType);
                        // Unbox obj (Ptr) to the field's primitive IL type.
                        if (right.type.kind == Type::Kind::Ptr && field->type)
                        {
                            Type fieldILType = mapType(field->type);
                            if (fieldILType.kind != Type::Kind::Ptr)
                                fieldValue = emitUnbox(fieldValue, fieldILType).value;
                        }
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

        // Handle index assignment (arr[i] = value, list[i] = value, map[key] = value)
        if (auto *indexExpr = dynamic_cast<IndexExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(indexExpr->base.get());
            auto index = lowerExpr(indexExpr->index.get());
            TypeRef baseType = sema_.typeOf(indexExpr->base.get());
            TypeRef indexRightType = sema_.typeOf(expr->right.get());

            // Fixed-size array: direct GEP + Store (no boxing, no runtime call)
            if (baseType && baseType->kind == TypeKindSem::FixedArray)
            {
                TypeRef elemType = baseType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);

                // Compute byte offset: index * elemSize
                unsigned mulId = nextTempId();
                il::core::Instr mulInstr;
                mulInstr.result = mulId;
                mulInstr.op = Opcode::Mul;
                mulInstr.type = Type(Type::Kind::I64);
                mulInstr.operands = {index.value, Value::constInt(static_cast<int64_t>(elemSize))};
                mulInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(mulInstr);
                Value byteOffset = Value::temp(mulId);

                // GEP to element address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {base.value, byteOffset};
                gepInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value elemAddr = Value::temp(gepId);

                // Store the element value
                il::core::Instr storeInstr;
                storeInstr.op = Opcode::Store;
                storeInstr.type = ilElemType;
                storeInstr.operands = {elemAddr, right.value};
                storeInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(storeInstr);
                return right;
            }

            Value boxedValue = emitBoxValue(right.value, right.type, indexRightType);
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
                const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(typeName);
                if (valueInfo)
                {
                    const FieldLayout *field = valueInfo->findField(fieldExpr->field);
                    if (field)
                    {
                        Value fieldValue =
                            wrapValueForOptionalField(right.value, field->type, rightType);
                        // Unbox obj (Ptr) to the field's primitive IL type.
                        if (right.type.kind == Type::Kind::Ptr && field->type)
                        {
                            Type fieldILType = mapType(field->type);
                            if (fieldILType.kind != Type::Kind::Ptr)
                                fieldValue = emitUnbox(fieldValue, fieldILType).value;
                        }
                        emitFieldStore(field, base.value, fieldValue);
                        return right;
                    }
                }

                // Check entity types
                const EntityTypeInfo *entityInfoPtr = getOrCreateEntityTypeInfo(typeName);
                if (entityInfoPtr)
                {
                    const FieldLayout *field = entityInfoPtr->findField(fieldExpr->field);
                    if (field)
                    {
                        Value fieldValue =
                            wrapValueForOptionalField(right.value, field->type, rightType);
                        // Unbox obj (Ptr) to the field's primitive IL type.
                        if (right.type.kind == Type::Kind::Ptr && field->type)
                        {
                            Type fieldILType = mapType(field->type);
                            if (fieldILType.kind != Type::Kind::Ptr)
                                fieldValue = emitUnbox(fieldValue, fieldILType).value;
                        }
                        emitFieldStore(field, base.value, fieldValue);
                        return right;
                    }
                }
            }
        }

        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Non-assignment binary operations

    // Handle short-circuit evaluation for And/Or BEFORE evaluating right operand
    if (expr->op == BinaryOp::And || expr->op == BinaryOp::Or)
    {
        return lowerShortCircuit(expr);
    }

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
        convInstr.loc = curLoc_;
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
        convInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(convInstr);
        right.value = Value::temp(convId);
        right.type = Type(Type::Kind::F64);
    }

    Opcode op = Opcode::Add;
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
                {
                    // Use kFmtBool to convert boolean to "true"/"false" string
                    rightStr = emitCallRet(Type(Type::Kind::Str), kFmtBool, {right.value});
                }

                Value result =
                    emitCallRet(Type(Type::Kind::Str), kStringConcat, {left.value, rightStr});
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
            op = isFloat ? Opcode::FDiv
                         : (options_.overflowChecks ? Opcode::SDivChk0 : Opcode::SDiv);
            break;

        case BinaryOp::Mod:
            op = options_.overflowChecks ? Opcode::SRemChk0 : Opcode::SRem;
            break;

        case BinaryOp::Eq:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String equality: rt_str_eq returns i1 (boolean) directly
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                return {result, Type(Type::Kind::I1)};
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
                // String inequality: rt_str_eq returns i1, zext to i64 then invert
                Value eqResult =
                    emitCallRet(Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                Value eqI64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eqResult);
                Value result =
                    emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), eqI64, Value::constInt(0));
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
                Value result =
                    emitCallRet(Type(Type::Kind::I64), "rt_str_lt", {left.value, right.value});
                Value boolResult =
                    emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Le:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for <=
                Value result =
                    emitCallRet(Type(Type::Kind::I64), "rt_str_le", {left.value, right.value});
                Value boolResult =
                    emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Gt:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for >
                Value result =
                    emitCallRet(Type(Type::Kind::I64), "rt_str_gt", {left.value, right.value});
                Value boolResult =
                    emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::Ge:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // Bug #003 fix: Use runtime string comparison for >=
                Value result =
                    emitCallRet(Type(Type::Kind::I64), "rt_str_ge", {left.value, right.value});
                Value boolResult =
                    emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, Value::constInt(0));
                return {boolResult, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            resultType = Type(Type::Kind::I1);
            break;

        case BinaryOp::And:
        {
            Value lhsExt = (left.type.kind == Type::Kind::I1)
                               ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value)
                               : left.value;
            Value rhsExt = (right.type.kind == Type::Kind::I1)
                               ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value)
                               : right.value;
            Value andResult = emitBinary(Opcode::And, Type(Type::Kind::I64), lhsExt, rhsExt);
            Value truncResult = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), andResult);
            return {truncResult, Type(Type::Kind::I1)};
        }

        case BinaryOp::Or:
        {
            Value lhsExt = (left.type.kind == Type::Kind::I1)
                               ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value)
                               : left.value;
            Value rhsExt = (right.type.kind == Type::Kind::I1)
                               ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value)
                               : right.value;
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
                Value result =
                    emitBinary(Opcode::FSub, operand.type, Value::constFloat(0.0), operand.value);
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
            Value result =
                emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), opVal, Value::constInt(0));
            return {result, Type(Type::Kind::I1)};
        }

        case UnaryOp::BitNot:
        {
            Value result =
                emitBinary(Opcode::Xor, operand.type, operand.value, Value::constInt(-1));
            return {result, operand.type};
        }

        case UnaryOp::AddressOf:
        {
            // Address-of operator for function references: &funcName
            // Returns a pointer to the function
            auto *ident = dynamic_cast<IdentExpr *>(expr->operand.get());
            if (!ident)
            {
                // Should have been caught in semantic analysis
                return {Value::constInt(0), Type(Type::Kind::Ptr)};
            }

            std::string mangledName = mangleFunctionName(ident->name);
            return {Value::global(mangledName), Type(Type::Kind::Ptr)};
        }
    }

    return operand;
}

//=============================================================================
// Short-Circuit Evaluation for And/Or
//=============================================================================

LowerResult Lowerer::lowerShortCircuit(BinaryExpr *expr)
{
    // Short-circuit evaluation for 'and' and 'or' operators.
    //
    // For 'A and B':
    //   - If A is false, result is false (don't evaluate B)
    //   - If A is true, result is B
    //
    // For 'A or B':
    //   - If A is true, result is true (don't evaluate B)
    //   - If A is false, result is B

    bool isAnd = (expr->op == BinaryOp::And);

    // Create basic blocks for control flow
    size_t evalRightIdx = createBlock(isAnd ? "and_rhs" : "or_rhs");
    size_t mergeIdx = createBlock(isAnd ? "and_merge" : "or_merge");

    // Allocate result slot
    unsigned slotId = nextTempId();
    il::core::Instr allocInstr;
    allocInstr.result = slotId;
    allocInstr.op = Opcode::Alloca;
    allocInstr.type = Type(Type::Kind::Ptr);
    allocInstr.operands = {Value::constInt(8)};
    allocInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocInstr);
    Value resultSlot = Value::temp(slotId);

    // Evaluate left operand
    auto left = lowerExpr(expr->left.get());

    // Extend to i64 for comparison if needed
    Value leftExt = (left.type.kind == Type::Kind::I1)
                        ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value)
                        : left.value;

    // Convert to bool (non-zero = true)
    Value leftBool = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), leftExt, Value::constInt(0));

    // Store left result as i1 in slot
    emitStore(resultSlot, leftBool, Type(Type::Kind::I1));

    // Branch based on left value
    // For 'and': if left is true, evaluate right; else short-circuit to merge
    // For 'or': if left is false, evaluate right; else short-circuit to merge
    if (isAnd)
    {
        emitCBr(leftBool, evalRightIdx, mergeIdx);
    }
    else
    {
        emitCBr(leftBool, mergeIdx, evalRightIdx);
    }

    // Evaluate right operand block
    setBlock(evalRightIdx);
    auto right = lowerExpr(expr->right.get());

    // Extend to i64 for comparison if needed
    Value rightExt = (right.type.kind == Type::Kind::I1)
                         ? emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value)
                         : right.value;

    // Convert to bool
    Value rightBool =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), rightExt, Value::constInt(0));

    // Store right result in slot
    emitStore(resultSlot, rightBool, Type(Type::Kind::I1));

    // Branch to merge
    emitBr(mergeIdx);

    // Merge block - load result from slot
    setBlock(mergeIdx);
    Value result = emitLoad(resultSlot, Type(Type::Kind::I1));

    return {result, Type(Type::Kind::I1)};
}

} // namespace il::frontends::zia
