//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr.cpp
/// @brief Expression lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
{

using namespace runtime;

//=============================================================================
// Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerExpr(Expr *expr)
{
    if (!expr)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            return lowerIntLiteral(static_cast<IntLiteralExpr *>(expr));
        case ExprKind::NumberLiteral:
            return lowerNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
        case ExprKind::StringLiteral:
            return lowerStringLiteral(static_cast<StringLiteralExpr *>(expr));
        case ExprKind::BoolLiteral:
            return lowerBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
        case ExprKind::NullLiteral:
            return lowerNullLiteral(static_cast<NullLiteralExpr *>(expr));
        case ExprKind::Ident:
            return lowerIdent(static_cast<IdentExpr *>(expr));
        case ExprKind::SelfExpr:
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::SuperExpr:
        {
            // Super returns self pointer but is used for dispatching to parent methods
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::Binary:
            return lowerBinary(static_cast<BinaryExpr *>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<UnaryExpr *>(expr));
        case ExprKind::Ternary:
            return lowerTernary(static_cast<TernaryExpr *>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<CallExpr *>(expr));
        case ExprKind::Field:
            return lowerField(static_cast<FieldExpr *>(expr));
        case ExprKind::New:
            return lowerNew(static_cast<NewExpr *>(expr));
        case ExprKind::Coalesce:
            return lowerCoalesce(static_cast<CoalesceExpr *>(expr));
        case ExprKind::OptionalChain:
            return lowerOptionalChain(static_cast<OptionalChainExpr *>(expr));
        case ExprKind::ListLiteral:
            return lowerListLiteral(static_cast<ListLiteralExpr *>(expr));
        case ExprKind::MapLiteral:
            return lowerMapLiteral(static_cast<MapLiteralExpr *>(expr));
        case ExprKind::Index:
            return lowerIndex(static_cast<IndexExpr *>(expr));
        case ExprKind::Try:
            return lowerTry(static_cast<TryExpr *>(expr));
        case ExprKind::Lambda:
            return lowerLambda(static_cast<LambdaExpr *>(expr));
        case ExprKind::Tuple:
            return lowerTuple(static_cast<TupleExpr *>(expr));
        case ExprKind::TupleIndex:
            return lowerTupleIndex(static_cast<TupleIndexExpr *>(expr));
        case ExprKind::Block:
            return lowerBlockExpr(static_cast<BlockExpr *>(expr));
        case ExprKind::Match:
            return lowerMatchExpr(static_cast<MatchExpr *>(expr));
        default:
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerIntLiteral(IntLiteralExpr *expr)
{
    return {Value::constInt(expr->value), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNumberLiteral(NumberLiteralExpr *expr)
{
    return {Value::constFloat(expr->value), Type(Type::Kind::F64)};
}

LowerResult Lowerer::lowerStringLiteral(StringLiteralExpr *expr)
{
    std::string globalName = getStringGlobal(expr->value);
    Value val = emitConstStr(globalName);
    return {val, Type(Type::Kind::Str)};
}

LowerResult Lowerer::lowerBoolLiteral(BoolLiteralExpr *expr)
{
    return {Value::constBool(expr->value), Type(Type::Kind::I1)};
}

LowerResult Lowerer::lowerNullLiteral(NullLiteralExpr * /*expr*/)
{
    return {Value::null(), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerIdent(IdentExpr *expr)
{
    // Check for slot-based mutable variables first (e.g., loop variables)
    auto slotIt = slots_.find(expr->name);
    if (slotIt != slots_.end())
    {
        TypeRef type = sema_.typeOf(expr);
        Type ilType = mapType(type);
        Value loaded = loadFromSlot(expr->name, ilType);
        return {loaded, ilType};
    }

    Value *local = lookupLocal(expr->name);
    if (local)
    {
        TypeRef type = sema_.typeOf(expr);
        return {*local, mapType(type)};
    }

    // Check for implicit field access (self.field) inside a value type method
    if (currentValueType_)
    {
        const FieldLayout *field = currentValueType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for implicit field access (self.field) inside an entity method
    if (currentEntityType_)
    {
        const FieldLayout *field = currentEntityType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for global constants (module-level const declarations)
    auto constIt = globalConstants_.find(expr->name);
    if (constIt != globalConstants_.end())
    {
        const Value &val = constIt->second;
        // Determine the type from the value kind
        Type ilType;
        switch (val.kind)
        {
            case Value::Kind::ConstFloat:
                ilType = Type(Type::Kind::F64);
                break;
            case Value::Kind::ConstStr:
            {
                // String constants need to emit a const_str instruction to load the global
                // The stored value's str field contains the global label (e.g., ".L10")
                Value loaded = emitConstStr(val.str);
                return {loaded, Type(Type::Kind::Str)};
            }
            case Value::Kind::GlobalAddr:
                ilType = Type(Type::Kind::Str);
                break;
            case Value::Kind::ConstInt:
                // Check if it's a boolean (i1) or integer (i64)
                ilType = val.isBool ? Type(Type::Kind::I1) : Type(Type::Kind::I64);
                break;
            default:
                ilType = Type(Type::Kind::I64);
                break;
        }
        return {val, ilType};
    }

    // Check for global mutable variables (module-level var declarations)
    auto globalIt = globalVariables_.find(expr->name);
    if (globalIt != globalVariables_.end())
    {
        TypeRef type = globalIt->second;
        Type ilType = mapType(type);
        Value addr = getGlobalVarAddr(expr->name, type);
        Value loaded = emitLoad(addr, ilType);
        return {loaded, ilType};
    }

    // Unknown identifier
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerBinary(BinaryExpr *expr)
{
    // Handle assignment specially
    if (expr->op == BinaryOp::Assign)
    {
        // Evaluate RHS first
        auto right = lowerExpr(expr->right.get());
        TypeRef rightType = sema_.typeOf(expr->right.get());

        // LHS must be an identifier for simple assignment
        if (auto *ident = dynamic_cast<IdentExpr *>(expr->left.get()))
        {
            TypeRef targetType = nullptr;
            auto typeIt = localTypes_.find(ident->name);
            if (typeIt != localTypes_.end())
            {
                targetType = typeIt->second;
            }
            else
            {
                targetType = sema_.typeOf(expr->left.get());
            }

            Value assignValue = right.value;
            Type assignType = right.type;
            if (targetType && targetType->kind == TypeKindSem::Optional)
            {
                TypeRef innerType = targetType->innerType();
                if (rightType && rightType->kind == TypeKindSem::Optional)
                {
                    assignType = Type(Type::Kind::Ptr);
                }
                else if (rightType && rightType->kind == TypeKindSem::Unit)
                {
                    assignValue = Value::null();
                    assignType = Type(Type::Kind::Ptr);
                }
                else if (innerType)
                {
                    assignValue = emitOptionalWrap(assignValue, innerType);
                    assignType = Type(Type::Kind::Ptr);
                }
            }

            // Check if this is a slot-based variable (e.g., mutable loop variable)
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                // Store to slot for mutable variables
                storeToSlot(ident->name, assignValue, assignType);
                return right;
            }

            // Check for implicit field assignment (self.field) inside a value type method
            if (currentValueType_)
            {
                const FieldLayout *field = currentValueType_->findField(ident->name);
                if (field)
                {
                    Value selfPtr;
                    if (getSelfPtr(selfPtr))
                    {
                        Value fieldValue = right.value;
                        if (field->type && field->type->kind == TypeKindSem::Optional)
                        {
                            TypeRef innerType = field->type->innerType();
                            if (rightType && rightType->kind == TypeKindSem::Optional)
                            {
                                fieldValue = right.value;
                            }
                            else if (rightType && rightType->kind == TypeKindSem::Unit)
                            {
                                fieldValue = Value::null();
                            }
                            else if (innerType)
                            {
                                fieldValue = emitOptionalWrap(right.value, innerType);
                            }
                        }
                        emitFieldStore(field, selfPtr, fieldValue);
                        return right;
                    }
                }
            }

            // Check for implicit field assignment (self.field) inside an entity method
            if (currentEntityType_)
            {
                const FieldLayout *field = currentEntityType_->findField(ident->name);
                if (field)
                {
                    Value selfPtr;
                    if (getSelfPtr(selfPtr))
                    {
                        Value fieldValue = right.value;
                        if (field->type && field->type->kind == TypeKindSem::Optional)
                        {
                            TypeRef innerType = field->type->innerType();
                            if (rightType && rightType->kind == TypeKindSem::Optional)
                            {
                                fieldValue = right.value;
                            }
                            else if (rightType && rightType->kind == TypeKindSem::Unit)
                            {
                                fieldValue = Value::null();
                            }
                            else if (innerType)
                            {
                                fieldValue = emitOptionalWrap(right.value, innerType);
                            }
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

                Value storeValue = assignValue;
                if (globalType && globalType->kind == TypeKindSem::Optional)
                {
                    TypeRef innerType = globalType->innerType();
                    if (rightType && rightType->kind == TypeKindSem::Optional)
                    {
                        storeValue = assignValue;
                    }
                    else if (rightType && rightType->kind == TypeKindSem::Unit)
                    {
                        storeValue = Value::null();
                    }
                    else if (innerType)
                    {
                        storeValue = emitOptionalWrap(assignValue, innerType);
                    }
                }
                emitStore(addr, storeValue, ilType);
                return right;
            }

            // Regular variable assignment
            defineLocal(ident->name, assignValue);
            if (targetType)
            {
                localTypes_[ident->name] = targetType;
            }
            return right;
        }

        // Handle index assignment (list[i] = value, map[key] = value)
        if (auto *indexExpr = dynamic_cast<IndexExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(indexExpr->base.get());
            auto index = lowerExpr(indexExpr->index.get());
            TypeRef baseType = sema_.typeOf(indexExpr->base.get());

            if (baseType && baseType->kind == TypeKindSem::Map)
            {
                // Map index assignment: map[key] = value
                Value boxedValue = emitBox(right.value, right.type);
                emitCall(kMapSet, {base.value, index.value, boxedValue});
                return right;
            }
            else
            {
                // List index assignment: list[i] = value
                Value boxedValue = emitBox(right.value, right.type);
                emitCall(kListSet, {base.value, index.value, boxedValue});
                return right;
            }
        }

        // Handle field assignment (obj.field = value)
        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->left.get()))
        {
            auto base = lowerExpr(fieldExpr->base.get());
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());

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
                        Value fieldValue = right.value;
                        if (field->type && field->type->kind == TypeKindSem::Optional)
                        {
                            TypeRef innerType = field->type->innerType();
                            if (rightType && rightType->kind == TypeKindSem::Optional)
                            {
                                fieldValue = right.value;
                            }
                            else if (rightType && rightType->kind == TypeKindSem::Unit)
                            {
                                fieldValue = Value::null();
                            }
                            else if (innerType)
                            {
                                fieldValue = emitOptionalWrap(right.value, innerType);
                            }
                        }
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
                        Value fieldValue = right.value;
                        if (field->type && field->type->kind == TypeKindSem::Optional)
                        {
                            TypeRef innerType = field->type->innerType();
                            if (rightType && rightType->kind == TypeKindSem::Optional)
                            {
                                fieldValue = right.value;
                            }
                            else if (rightType && rightType->kind == TypeKindSem::Unit)
                            {
                                fieldValue = Value::null();
                            }
                            else if (innerType)
                            {
                                fieldValue = emitOptionalWrap(right.value, innerType);
                            }
                        }
                        emitFieldStore(field, base.value, fieldValue);
                        return right;
                    }
                }
            }
        }

        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto left = lowerExpr(expr->left.get());
    auto right = lowerExpr(expr->right.get());

    TypeRef leftType = sema_.typeOf(expr->left.get());
    bool isFloat = leftType && leftType->kind == TypeKindSem::Number;

    Opcode op;
    Type resultType = left.type;

    switch (expr->op)
    {
        case BinaryOp::Add:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String concatenation - convert right operand to string if needed
                TypeRef rightType = sema_.typeOf(expr->right.get());
                Value rightStr = right.value;

                if (rightType && rightType->kind == TypeKindSem::Integer)
                {
                    // Convert integer to string
                    rightStr =
                        emitCallRet(Type(Type::Kind::Str), runtime::kStringFromInt, {right.value});
                }
                else if (rightType && rightType->kind == TypeKindSem::Number)
                {
                    // Convert number to string
                    rightStr =
                        emitCallRet(Type(Type::Kind::Str), runtime::kStringFromNum, {right.value});
                }
                else if (rightType && rightType->kind == TypeKindSem::Boolean)
                {
                    // Convert boolean to string: use "true" or "false"
                    // For now, just convert 0/1 to string
                    rightStr =
                        emitCallRet(Type(Type::Kind::Str), runtime::kStringFromInt, {right.value});
                }

                Value result = emitCallRet(
                    Type(Type::Kind::Str), runtime::kStringConcat, {left.value, rightStr});
                return {result, Type(Type::Kind::Str)};
            }
            if (isFloat)
            {
                op = Opcode::FAdd;
            }
            else
            {
                op = options_.overflowChecks ? Opcode::IAddOvf : Opcode::Add;
            }
            break;
        case BinaryOp::Sub:
            if (isFloat)
            {
                op = Opcode::FSub;
            }
            else
            {
                op = options_.overflowChecks ? Opcode::ISubOvf : Opcode::Sub;
            }
            break;
        case BinaryOp::Mul:
            if (isFloat)
            {
                op = Opcode::FMul;
            }
            else
            {
                op = options_.overflowChecks ? Opcode::IMulOvf : Opcode::Mul;
            }
            break;
        case BinaryOp::Div:
            if (isFloat)
            {
                op = Opcode::FDiv;
            }
            else
            {
                op = options_.overflowChecks ? Opcode::SDivChk0 : Opcode::SDiv;
            }
            break;
        case BinaryOp::Mod:
            op = options_.overflowChecks ? Opcode::SRemChk0 : Opcode::SRem;
            break;
        case BinaryOp::Eq:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String equality comparison via runtime
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                return {result, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Ne:
            if (leftType && leftType->kind == TypeKindSem::String)
            {
                // String inequality: !(a == b)
                // Get equals result (returns i1)
                Value eqResult =
                    emitCallRet(Type(Type::Kind::I1), kStringEquals, {left.value, right.value});
                // Zero-extend to i64 for comparison (ICmpEq expects i64 operands)
                Value extResult = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eqResult);
                // Compare with 0 to negate: !(a == b) means (a == b) == 0
                Value result =
                    emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), extResult, Value::constInt(0));
                return {result, Type(Type::Kind::I1)};
            }
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            resultType = Type(Type::Kind::I1);
            break;
        case BinaryOp::And:
        {
            // Boolean AND: operands may be I1 from comparisons, but And opcode expects I64.
            // Zero-extend both operands to I64, perform And, then truncate back to I1.
            Value lhsExt = left.value;
            Value rhsExt = right.value;

            if (left.type.kind == Type::Kind::I1)
            {
                lhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value);
            }
            if (right.type.kind == Type::Kind::I1)
            {
                rhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value);
            }

            Value andResult = emitBinary(Opcode::And, Type(Type::Kind::I64), lhsExt, rhsExt);
            Value truncResult = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), andResult);
            return {truncResult, Type(Type::Kind::I1)};
        }
        case BinaryOp::Or:
        {
            // Boolean OR: same treatment as AND.
            Value lhsExt = left.value;
            Value rhsExt = right.value;

            if (left.type.kind == Type::Kind::I1)
            {
                lhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), left.value);
            }
            if (right.type.kind == Type::Kind::I1)
            {
                rhsExt = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), right.value);
            }

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

LowerResult Lowerer::lowerUnary(UnaryExpr *expr)
{
    auto operand = lowerExpr(expr->operand.get());
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    bool isFloat = operandType && operandType->kind == TypeKindSem::Number;

    switch (expr->op)
    {
        case UnaryOp::Neg:
        {
            // Synthesize negation: 0 - x (no INeg/FNeg opcode)
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
            // Boolean NOT: compare with 0 (false)
            // ICmpEq expects I64 operands, so zero-extend I1 values first
            Value opVal = operand.value;
            if (operand.type.kind == Type::Kind::I1)
            {
                opVal = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), operand.value);
            }
            Value result =
                emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), opVal, Value::constInt(0));
            return {result, Type(Type::Kind::I1)};
        }
        case UnaryOp::BitNot:
        {
            // Bitwise NOT: XOR with -1 (all bits set)
            Value result =
                emitBinary(Opcode::Xor, operand.type, operand.value, Value::constInt(-1));
            return {result, operand.type};
        }
    }

    return operand;
}

LowerResult Lowerer::lowerCall(CallExpr *expr)
{
    // Check for method call on value or entity type: obj.method()
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
    {
        // Check for super.method() call - dispatch to parent class method
        if (fieldExpr->base->kind == ExprKind::SuperExpr)
        {
            // Get self pointer
            Value selfPtr;
            if (getSelfPtr(selfPtr) && currentEntityType_ && !currentEntityType_->baseClass.empty())
            {
                // Look up method in the parent class
                auto parentIt = entityTypes_.find(currentEntityType_->baseClass);
                if (parentIt != entityTypes_.end())
                {
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field))
                    {
                        return lowerMethodCall(
                            method, currentEntityType_->baseClass, selfPtr, expr);
                    }
                }
            }
        }

        // Get the type of the base expression
        TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
        if (baseType)
        {
            std::string typeName = baseType->name;

            // Check value type methods (O(1) lookup)
            auto it = valueTypes_.find(typeName);
            if (it != valueTypes_.end())
            {
                if (auto *method = it->second.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check entity type methods (O(1) lookup)
            auto entityIt = entityTypes_.find(typeName);
            if (entityIt != entityTypes_.end())
            {
                if (auto *method = entityIt->second.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check for method call on List type: list.add(), list.get(), etc.
            if (baseType->kind == TypeKindSem::List)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                std::string methodName = fieldExpr->field;

                if (equalsIgnoreCase(methodName, "get"))
                {
                    if (expr->args.size() >= 1)
                    {
                        auto indexResult = lowerExpr(expr->args[0].value.get());
                        Value boxed = emitCallRet(
                            Type(Type::Kind::Ptr), kListGet, {baseResult.value, indexResult.value});
                        TypeRef elemType = baseType->elementType();
                        if (elemType)
                        {
                            Type ilElemType = mapType(elemType);
                            return emitUnbox(boxed, ilElemType);
                        }
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                }

                if (equalsIgnoreCase(methodName, "removeAt"))
                {
                    if (expr->args.size() >= 1)
                    {
                        auto indexResult = lowerExpr(expr->args[0].value.get());
                        emitCall(kListRemoveAt, {baseResult.value, indexResult.value});
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                }

                if (equalsIgnoreCase(methodName, "has") || equalsIgnoreCase(methodName, "contains"))
                {
                    if (expr->args.size() >= 1)
                    {
                        auto valueResult = lowerExpr(expr->args[0].value.get());
                        Value boxedValue = emitBox(valueResult.value, valueResult.type);
                        Value result = emitCallRet(
                            Type(Type::Kind::I1), kListContains, {baseResult.value, boxedValue});
                        return {result, Type(Type::Kind::I1)};
                    }
                }

                // Lower arguments with boxing
                std::vector<Value> args;
                args.reserve(expr->args.size() + 1);
                args.push_back(baseResult.value); // list is first argument

                for (auto &arg : expr->args)
                {
                    auto result = lowerExpr(arg.value.get());
                    args.push_back(emitBox(result.value, result.type));
                }

                // Map method names to runtime functions (case-insensitive)
                const char *runtimeFunc = nullptr;
                Type returnType = Type(Type::Kind::Void);

                if (equalsIgnoreCase(methodName, "add"))
                {
                    runtimeFunc = kListAdd;
                }
                else if (equalsIgnoreCase(methodName, "size") ||
                         equalsIgnoreCase(methodName, "count") ||
                         equalsIgnoreCase(methodName, "length"))
                {
                    runtimeFunc = kListCount;
                    returnType = Type(Type::Kind::I64);
                }
                else if (equalsIgnoreCase(methodName, "clear"))
                {
                    runtimeFunc = kListClear;
                }
                else if (equalsIgnoreCase(methodName, "set"))
                {
                    // list.set(index, value) - needs index (as boxed) and boxed value
                    if (expr->args.size() >= 2)
                    {
                        // args already has: [list, boxedArg0, boxedArg1, ...]
                        // We need: list, index, boxedValue
                        // Index should be unboxed i64, value should be boxed
                        auto indexResult = lowerExpr(expr->args[0].value.get());
                        auto valueResult = lowerExpr(expr->args[1].value.get());
                        Value boxedValue = emitBox(valueResult.value, valueResult.type);
                        emitCall(kListSet, {baseResult.value, indexResult.value, boxedValue});
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                }

                if (runtimeFunc != nullptr)
                {
                    if (returnType.kind == Type::Kind::Void)
                    {
                        emitCall(runtimeFunc, args);
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                    else if (returnType.kind == Type::Kind::Ptr)
                    {
                        // Returns boxed value, need to unbox based on element type
                        Value boxed = emitCallRet(returnType, runtimeFunc, args);
                        TypeRef elemType = baseType->elementType();
                        if (elemType)
                        {
                            Type ilElemType = mapType(elemType);
                            return emitUnbox(boxed, ilElemType);
                        }
                        // Return the boxed/object value as-is for entity types
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                    else
                    {
                        Value result = emitCallRet(returnType, runtimeFunc, args);
                        return {result, returnType};
                    }
                }
            }

            // Check for method call on Map type: map.set(), map.get(), map.containsKey(), etc.
            if (baseType->kind == TypeKindSem::Map)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                std::string methodName = fieldExpr->field;

                TypeRef valueType = baseType->typeArgs.size() > 1 ? baseType->typeArgs[1] : nullptr;

                if (equalsIgnoreCase(methodName, "set") || equalsIgnoreCase(methodName, "put"))
                {
                    // map.set(key, value) - key is string, value is boxed
                    if (expr->args.size() >= 2)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        auto valueResult = lowerExpr(expr->args[1].value.get());
                        Value boxedValue = emitBox(valueResult.value, valueResult.type);
                        emitCall(kMapSet, {baseResult.value, keyResult.value, boxedValue});
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "get"))
                {
                    // map.get(key) - returns boxed value
                    if (expr->args.size() >= 1)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        Value boxed = emitCallRet(
                            Type(Type::Kind::Ptr), kMapGet, {baseResult.value, keyResult.value});
                        if (valueType)
                        {
                            Type ilValueType = mapType(valueType);
                            return emitUnbox(boxed, ilValueType);
                        }
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "getOr"))
                {
                    // map.getOr(key, defaultValue) - returns boxed value
                    if (expr->args.size() >= 2)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        auto defaultResult = lowerExpr(expr->args[1].value.get());
                        Value boxedDefault = emitBox(defaultResult.value, defaultResult.type);
                        Value boxed =
                            emitCallRet(Type(Type::Kind::Ptr),
                                        kMapGetOr,
                                        {baseResult.value, keyResult.value, boxedDefault});
                        if (valueType)
                        {
                            Type ilValueType = mapType(valueType);
                            return emitUnbox(boxed, ilValueType);
                        }
                        return {boxed, Type(Type::Kind::Ptr)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "containsKey") ||
                         equalsIgnoreCase(methodName, "hasKey") ||
                         equalsIgnoreCase(methodName, "has"))
                {
                    if (expr->args.size() >= 1)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        Value result = emitCallRet(Type(Type::Kind::I1),
                                                   kMapContainsKey,
                                                   {baseResult.value, keyResult.value});
                        return {result, Type(Type::Kind::I1)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "size") ||
                         equalsIgnoreCase(methodName, "count") ||
                         equalsIgnoreCase(methodName, "length"))
                {
                    Value result =
                        emitCallRet(Type(Type::Kind::I64), kMapCount, {baseResult.value});
                    return {result, Type(Type::Kind::I64)};
                }
                else if (equalsIgnoreCase(methodName, "remove"))
                {
                    if (expr->args.size() >= 1)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        Value result = emitCallRet(
                            Type(Type::Kind::I1), kMapRemove, {baseResult.value, keyResult.value});
                        return {result, Type(Type::Kind::I1)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "setIfMissing"))
                {
                    if (expr->args.size() >= 2)
                    {
                        auto keyResult = lowerExpr(expr->args[0].value.get());
                        auto valueResult = lowerExpr(expr->args[1].value.get());
                        Value boxedValue = emitBox(valueResult.value, valueResult.type);
                        Value result = emitCallRet(Type(Type::Kind::I1),
                                                   kMapSetIfMissing,
                                                   {baseResult.value, keyResult.value, boxedValue});
                        return {result, Type(Type::Kind::I1)};
                    }
                }
                else if (equalsIgnoreCase(methodName, "clear"))
                {
                    emitCall(kMapClear, {baseResult.value});
                    return {Value::constInt(0), Type(Type::Kind::Void)};
                }
                else if (equalsIgnoreCase(methodName, "keys"))
                {
                    Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapKeys, {baseResult.value});
                    return {seq, Type(Type::Kind::Ptr)};
                }
                else if (equalsIgnoreCase(methodName, "values"))
                {
                    Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapValues, {baseResult.value});
                    return {seq, Type(Type::Kind::Ptr)};
                }
            }
        }
    }

    // Check if this is a resolved runtime call (e.g., Viper.Terminal.Say)
    std::string runtimeCallee = sema_.runtimeCallee(expr);
    if (!runtimeCallee.empty())
    {
        // Lower arguments
        std::vector<Value> args;
        args.reserve(expr->args.size());
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            args.push_back(result.value);
        }

        // Get return type from expression
        TypeRef exprType = sema_.typeOf(expr);
        Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(runtimeCallee, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, runtimeCallee, args);
            return {result, ilReturnType};
        }
    }

    // Check for built-in functions
    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        if (ident->name == "print" || ident->name == "println")
        {
            if (!expr->args.empty())
            {
                auto arg = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());

                // Convert to string if needed
                Value strVal = arg.value;
                if (argType && argType->kind != TypeKindSem::String)
                {
                    if (argType->kind == TypeKindSem::Integer)
                    {
                        strVal = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                    }
                    else if (argType->kind == TypeKindSem::Number)
                    {
                        strVal = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                    }
                }

                emitCall(kTerminalSay, {strVal});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        if (ident->name == "toString")
        {
            if (expr->args.empty())
                return {Value::constInt(0), Type(Type::Kind::Str)};

            auto *argExpr = expr->args[0].value.get();
            auto arg = lowerExpr(argExpr);
            TypeRef argType = sema_.typeOf(argExpr);

            if (argType)
            {
                switch (argType->kind)
                {
                    case TypeKindSem::String:
                        return {arg.value, Type(Type::Kind::Str)};
                    case TypeKindSem::Integer:
                    {
                        Value strVal =
                            emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                        return {strVal, Type(Type::Kind::Str)};
                    }
                    case TypeKindSem::Number:
                    {
                        Value strVal =
                            emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                        return {strVal, Type(Type::Kind::Str)};
                    }
                    case TypeKindSem::Boolean:
                    {
                        Value strVal = emitCallRet(Type(Type::Kind::Str), kFmtBool, {arg.value});
                        return {strVal, Type(Type::Kind::Str)};
                    }
                    default:
                        break;
                }
            }

            if (arg.type.kind == Type::Kind::Ptr)
            {
                Value strVal = emitCallRet(Type(Type::Kind::Str), kObjectToString, {arg.value});
                return {strVal, Type(Type::Kind::Str)};
            }

            return {Value::constInt(0), Type(Type::Kind::Str)};
        }

        // Check for value type construction
        auto it = valueTypes_.find(ident->name);
        if (it != valueTypes_.end())
        {
            const ValueTypeInfo &info = it->second;

            // Lower arguments
            std::vector<Value> argValues;
            for (auto &arg : expr->args)
            {
                auto result = lowerExpr(arg.value.get());
                argValues.push_back(result.value);
            }

            // Allocate stack space for the value
            unsigned allocaId = nextTempId();
            il::core::Instr allocaInstr;
            allocaInstr.result = allocaId;
            allocaInstr.op = Opcode::Alloca;
            allocaInstr.type = Type(Type::Kind::Ptr);
            allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
            blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
            Value ptr = Value::temp(allocaId);

            // Store each argument into the corresponding field
            for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
            {
                const FieldLayout &field = info.fields[i];

                // GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Store the value
                il::core::Instr storeInstr;
                storeInstr.op = Opcode::Store;
                storeInstr.type = mapType(field.type);
                storeInstr.operands = {fieldAddr, argValues[i]};
                blockMgr_.currentBlock()->instructions.push_back(storeInstr);
            }

            // Return pointer to the constructed value
            return {ptr, Type(Type::Kind::Ptr)};
        }
    }

    // Get callee name and check if it's a function or a variable holding a function pointer
    std::string calleeName;
    bool isIndirectCall = false;
    Value funcPtr;

    // Check if callee type is a function/lambda type (for closure handling)
    TypeRef calleeType = sema_.typeOf(expr->callee.get());
    bool isLambdaClosure = calleeType && calleeType->isCallable();

    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        // Check if this is a variable holding a function pointer (not a defined function)
        if (definedFunctions_.find(mangleFunctionName(ident->name)) == definedFunctions_.end())
        {
            // It's a variable - need indirect call
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                // Load the closure pointer from the slot
                unsigned loadId = nextTempId();
                il::core::Instr loadInstr;
                loadInstr.result = loadId;
                loadInstr.op = Opcode::Load;
                loadInstr.type = Type(Type::Kind::Ptr);
                loadInstr.operands = {slotIt->second};
                blockMgr_.currentBlock()->instructions.push_back(loadInstr);
                funcPtr = Value::temp(loadId);
                isIndirectCall = true;
            }
            else
            {
                // Check in locals
                auto localIt = locals_.find(ident->name);
                if (localIt != locals_.end())
                {
                    funcPtr = localIt->second;
                    isIndirectCall = true;
                }
            }
        }

        if (!isIndirectCall)
        {
            calleeName = mangleFunctionName(ident->name);
        }
    }
    else
    {
        // For other expressions, lower them to get a closure pointer
        auto calleeResult = lowerExpr(expr->callee.get());
        funcPtr = calleeResult.value;
        isIndirectCall = true;
    }

    // Get return type
    TypeRef returnType = calleeType ? calleeType->returnType() : nullptr;
    Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

    // Lower arguments (with optional wrapping when needed)
    std::vector<TypeRef> paramTypes;
    if (calleeType)
        paramTypes = calleeType->paramTypes();

    std::vector<Value> args;
    args.reserve(expr->args.size());
    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        if (i < paramTypes.size())
        {
            TypeRef paramType = paramTypes[i];
            TypeRef argType = sema_.typeOf(arg.value.get());
            if (paramType && paramType->kind == TypeKindSem::Optional)
            {
                TypeRef innerType = paramType->innerType();
                if (argType && argType->kind == TypeKindSem::Optional)
                {
                    argValue = result.value;
                }
                else if (argType && argType->kind == TypeKindSem::Unit)
                {
                    argValue = Value::null();
                }
                else if (innerType)
                {
                    argValue = emitOptionalWrap(result.value, innerType);
                }
            }
        }

        args.push_back(argValue);
    }

    if (isIndirectCall)
    {
        // For lambda closures, unpack the closure struct { funcPtr, envPtr }
        if (isLambdaClosure)
        {
            // funcPtr currently points to the closure struct
            Value closurePtr = funcPtr;

            // Load the actual function pointer from offset 0
            Value actualFuncPtr = emitLoad(closurePtr, Type(Type::Kind::Ptr));

            // Load the environment pointer from offset 8
            Value envFieldAddr = emitGEP(closurePtr, 8);
            Value envPtr = emitLoad(envFieldAddr, Type(Type::Kind::Ptr));

            // Prepend env to args (all lambdas expect __env as first param)
            std::vector<Value> closureArgs;
            closureArgs.reserve(args.size() + 1);
            closureArgs.push_back(envPtr);
            for (const auto &arg : args)
            {
                closureArgs.push_back(arg);
            }

            // Make the indirect call with env as first argument
            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(actualFuncPtr, closureArgs);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, actualFuncPtr, closureArgs);
                return {result, ilReturnType};
            }
        }
        else
        {
            // Non-closure indirect call (shouldn't happen often)
            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(funcPtr, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, funcPtr, args);
                return {result, ilReturnType};
            }
        }
    }
    else
    {
        // Direct call to named function
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(calleeName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, calleeName, args);
            return {result, ilReturnType};
        }
    }
}

LowerResult Lowerer::lowerField(FieldExpr *expr)
{
    // Lower the base expression
    auto base = lowerExpr(expr->base.get());

    // Get the type of the base expression
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Check if base is a value type
    std::string typeName = baseType->name;
    auto it = valueTypes_.find(typeName);
    if (it != valueTypes_.end())
    {
        const ValueTypeInfo &info = it->second;
        const FieldLayout *field = info.findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Check if base is an entity type
    auto entityIt = entityTypes_.find(typeName);
    if (entityIt != entityTypes_.end())
    {
        const EntityTypeInfo &info = entityIt->second;
        const FieldLayout *field = info.findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Unknown field access
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNew(NewExpr *expr)
{
    // Get the type from the new expression
    TypeRef type = sema_.resolveType(expr->type.get());
    if (!type)
    {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    // Handle built-in collection types
    if (type->kind == TypeKindSem::List)
    {
        // Create a new list via runtime
        Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
        return {list, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Set)
    {
        Value set = emitCallRet(Type(Type::Kind::Ptr), kSetNew, {});
        return {set, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Map)
    {
        Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});
        return {map, Type(Type::Kind::Ptr)};
    }

    // Find the entity type info
    std::string typeName = type->name;
    auto it = entityTypes_.find(typeName);
    if (it == entityTypes_.end())
    {
        // Not an entity type
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    const EntityTypeInfo &info = it->second;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate heap memory for the entity using rt_obj_new_i64
    // This properly initializes the heap header with magic, refcount, etc.
    // so that entities can be added to lists and other reference-counted collections
    Value ptr = emitCallRet(Type(Type::Kind::Ptr),
                            "rt_obj_new_i64",
                            {Value::constInt(static_cast<int64_t>(info.classId)),
                             Value::constInt(static_cast<int64_t>(info.totalSize))});

    // Store each argument into the corresponding field
    for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
    {
        const FieldLayout &field = info.fields[i];

        // GEP to get field address
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        Value fieldAddr = Value::temp(gepId);

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = mapType(field.type);
        storeInstr.operands = {fieldAddr, argValues[i]};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }

    // Return pointer to the allocated entity
    return {ptr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerCoalesce(CoalesceExpr *expr)
{
    // Get the type to determine how to handle the coalesce
    TypeRef leftType = sema_.typeOf(expr->left.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;
    TypeRef innerType = resultType;

    // For reference types (entities, etc.), check if the pointer is null
    // For value-type optionals, we would need to check the flag field
    // Currently implementing reference-type coalesce

    // Allocate a stack slot for the result BEFORE branching
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for ptr/i64
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    // Lower the left expression
    auto left = lowerExpr(expr->left.get());

    // Create blocks for the coalesce
    size_t hasValueIdx = createBlock("coalesce_has");
    size_t isNullIdx = createBlock("coalesce_null");
    size_t mergeIdx = createBlock("coalesce_merge");

    // Check if it's null (for reference types, compare pointer to 0)
    // Note: ICmpNe requires i64 operands, so we convert the pointer via alloca/store/load
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, left.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, isNullIdx);

    // Has value block - store left value and branch to merge
    setBlock(hasValueIdx);
    {
        Value unwrapped = left.value;
        if (innerType)
        {
            auto innerVal = emitOptionalUnwrap(left.value, innerType);
            unwrapped = innerVal.value;
        }
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, unwrapped};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Is null block - evaluate right, store, and branch to merge
    setBlock(isNullIdx);
    auto right = lowerExpr(expr->right.get());
    {
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, right.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Merge block - load the result
    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

LowerResult Lowerer::lowerTernary(TernaryExpr *expr)
{
    auto cond = lowerExpr(expr->condition.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Allocate a stack slot for the result before branching.
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    size_t thenIdx = createBlock("ternary_then");
    size_t elseIdx = createBlock("ternary_else");
    size_t mergeIdx = createBlock("ternary_merge");

    emitCBr(cond.value, thenIdx, elseIdx);

    setBlock(thenIdx);
    {
        auto thenResult = lowerExpr(expr->thenExpr.get());
        Value thenValue = thenResult.value;
        if (expectsOptional)
        {
            TypeRef thenType = sema_.typeOf(expr->thenExpr.get());
            if (!thenType || thenType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    thenValue = emitOptionalWrap(thenResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, thenValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(elseIdx);
    {
        auto elseResult = lowerExpr(expr->elseExpr.get());
        Value elseValue = elseResult.value;
        if (expectsOptional)
        {
            TypeRef elseType = sema_.typeOf(expr->elseExpr.get());
            if (!elseType || elseType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    elseValue = emitOptionalWrap(elseResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, elseValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    if (ilResultType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

LowerResult Lowerer::lowerOptionalChain(OptionalChainExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType || baseType->kind != TypeKindSem::Optional)
    {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    TypeRef innerType = baseType->innerType();
    TypeRef fieldType = types::unknown();

    // Allocate a stack slot for the result (optional pointer)
    unsigned resultSlotId = nextTempId();
    il::core::Instr resultAlloca;
    resultAlloca.result = resultSlotId;
    resultAlloca.op = Opcode::Alloca;
    resultAlloca.type = Type(Type::Kind::Ptr);
    resultAlloca.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(resultAlloca);
    Value resultSlot = Value::temp(resultSlotId);

    // Compare optional pointer with null
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, base.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNull = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));

    size_t hasValueIdx = createBlock("optchain_has");
    size_t isNullIdx = createBlock("optchain_null");
    size_t mergeIdx = createBlock("optchain_merge");
    emitCBr(isNull, isNullIdx, hasValueIdx);

    // Null block
    setBlock(isNullIdx);
    il::core::Instr storeNull;
    storeNull.op = Opcode::Store;
    storeNull.type = Type(Type::Kind::Ptr);
    storeNull.operands = {resultSlot, Value::null()};
    blockMgr_.currentBlock()->instructions.push_back(storeNull);
    emitBr(mergeIdx);

    // Has value block
    setBlock(hasValueIdx);
    Value fieldValue = Value::null();
    if (innerType)
    {
        if (innerType->kind == TypeKindSem::Value || innerType->kind == TypeKindSem::Entity)
        {
            const std::map<std::string, ValueTypeInfo> &valueTypes = valueTypes_;
            const std::map<std::string, EntityTypeInfo> &entityTypes = entityTypes_;
            if (innerType->kind == TypeKindSem::Value)
            {
                auto it = valueTypes.find(innerType->name);
                if (it != valueTypes.end())
                {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field)
                    {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            }
            else
            {
                auto it = entityTypes.find(innerType->name);
                if (it != entityTypes.end())
                {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field)
                    {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            }
        }
        else if (innerType->kind == TypeKindSem::List)
        {
            if (expr->field == "count" || expr->field == "size" || expr->field == "length")
            {
                fieldType = types::integer();
                fieldValue = emitCallRet(Type(Type::Kind::I64), kListCount, {base.value});
            }
        }
    }

    Value optionalValue = Value::null();
    if (fieldType && fieldType->kind == TypeKindSem::Optional)
    {
        optionalValue = fieldValue;
    }
    else if (fieldType && fieldType->kind != TypeKindSem::Unknown)
    {
        optionalValue = emitOptionalWrap(fieldValue, fieldType);
    }

    il::core::Instr storeVal;
    storeVal.op = Opcode::Store;
    storeVal.type = Type(Type::Kind::Ptr);
    storeVal.operands = {resultSlot, optionalValue};
    blockMgr_.currentBlock()->instructions.push_back(storeVal);
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = Type(Type::Kind::Ptr);
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerListLiteral(ListLiteralExpr *expr)
{
    // Create a new list
    Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});

    // Add each element to the list (boxed)
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());
        Value boxed = emitBox(result.value, result.type);
        emitCall(kListAdd, {list, boxed});
    }

    return {list, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerMapLiteral(MapLiteralExpr *expr)
{
    Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});

    for (auto &entry : expr->entries)
    {
        auto keyResult = lowerExpr(entry.key.get());
        auto valueResult = lowerExpr(entry.value.get());
        Value boxedValue = emitBox(valueResult.value, valueResult.type);
        emitCall(kMapSet, {map, keyResult.value, boxedValue});
    }

    return {map, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTuple(TupleExpr *expr)
{
    // Get the tuple type from sema
    TypeRef tupleType = sema_.typeOf(expr);

    // Calculate total size (assuming 8 bytes per element for simplicity)
    size_t tupleSize = tupleType->tupleElementTypes().size() * 8;

    // Allocate space for the tuple on the stack
    unsigned slotId = nextTempId();
    il::core::Instr allocInstr;
    allocInstr.result = slotId;
    allocInstr.op = Opcode::Alloca;
    allocInstr.type = Type(Type::Kind::Ptr);
    allocInstr.operands = {Value::constInt(static_cast<int64_t>(tupleSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocInstr);
    Value tuplePtr = Value::temp(slotId);

    // Store each element in the tuple
    size_t offset = 0;
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());

        // Calculate element pointer
        Value elemPtr = tuplePtr;
        if (offset > 0)
        {
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {tuplePtr, Value::constInt(static_cast<int64_t>(offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            elemPtr = Value::temp(gepId);
        }

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = result.type;
        storeInstr.operands = {elemPtr, result.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);

        offset += 8;
    }

    return {tuplePtr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTupleIndex(TupleIndexExpr *expr)
{
    // Lower the tuple expression
    auto tupleResult = lowerExpr(expr->tuple.get());

    // Get the tuple type to determine element type
    TypeRef tupleType = sema_.typeOf(expr->tuple.get());
    TypeRef elemType = tupleType->tupleElementType(expr->index);
    Type ilType = mapType(elemType);

    // Calculate offset (assuming 8 bytes per element)
    size_t offset = expr->index * 8;

    // Calculate element pointer
    Value elemPtr = tupleResult.value;
    if (offset > 0)
    {
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {tupleResult.value, Value::constInt(static_cast<int64_t>(offset))};
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        elemPtr = Value::temp(gepId);
    }

    // Load the element value
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilType;
    loadInstr.operands = {elemPtr};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilType};
}

LowerResult Lowerer::lowerIndex(IndexExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    auto index = lowerExpr(expr->index.get());

    // Get the base type to determine if it's a List or Map
    TypeRef baseType = sema_.typeOf(expr->base.get());

    Value boxed;
    if (baseType && baseType->kind == TypeKindSem::Map)
    {
        // Map index access - key is a string
        boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet, {base.value, index.value});
    }
    else
    {
        // List index access (default)
        boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {base.value, index.value});
    }

    // Get the expected element type from semantic analysis
    TypeRef elemType = sema_.typeOf(expr);
    Type ilType = mapType(elemType);

    return emitUnbox(boxed, ilType);
}

LowerResult Lowerer::lowerTry(TryExpr *expr)
{
    // The ? operator propagates null/error by returning early from the function
    // For now, we implement this for optional types (null propagation)

    auto operand = lowerExpr(expr->operand.get());

    // Create blocks for the null check
    size_t hasValueIdx = createBlock("try.hasvalue");
    size_t returnNullIdx = createBlock("try.returnnull");

    // Check if the value is null (comparing pointer as i64 to 0)
    // First, store the pointer and load as i64 for comparison
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, operand.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, returnNullIdx);

    // Return null block - return null from the current function
    setBlock(returnNullIdx);
    // For functions returning optional types, return null (0 as pointer)
    // For void functions, we just return void
    if (currentFunc_->retType.kind == Type::Kind::Void)
    {
        emitRetVoid();
    }
    else
    {
        // Return null for optional/pointer return types
        emitRet(Value::constInt(0));
    }

    // Has value block - continue with the unwrapped value
    setBlock(hasValueIdx);

    // Return the operand value (unwrap optionals when needed)
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    if (operandType && operandType->kind == TypeKindSem::Optional)
    {
        TypeRef innerType = operandType->innerType();
        if (innerType)
            return emitOptionalUnwrap(operand.value, innerType);
    }
    return operand;
}

LowerResult Lowerer::lowerLambda(LambdaExpr *expr)
{
    // Generate unique lambda function name
    static int lambdaCounter = 0;
    std::string lambdaName = "__lambda_" + std::to_string(lambdaCounter++);

    // Check if lambda has captured variables
    bool hasCaptures = !expr->captures.empty();

    // Determine return type (inferred as the body's type if not specified)
    TypeRef returnType = types::unknown();
    if (expr->returnType)
    {
        returnType = sema_.resolveType(expr->returnType.get());
    }
    else
    {
        returnType = sema_.typeOf(expr->body.get());
    }
    Type ilReturnType = mapType(returnType);

    // Build parameter list - always add env pointer as first param for uniform closure ABI
    std::vector<il::core::Param> params;
    params.reserve(expr->params.size() + 1);
    params.push_back({"__env", Type(Type::Kind::Ptr)});
    for (const auto &param : expr->params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Collect info about captured variables before switching contexts
    // We need to capture their current values/slot pointers
    struct CaptureInfo
    {
        std::string name;
        Value value;
        Type type;
        TypeRef semType;
        bool isSlot;
    };

    std::vector<CaptureInfo> captureInfos;
    if (hasCaptures)
    {
        for (const auto &cap : expr->captures)
        {
            CaptureInfo info;
            info.name = cap.name;
            info.isSlot = false;

            // Look up the variable in current scope
            auto slotIt = slots_.find(cap.name);
            if (slotIt != slots_.end())
            {
                // Load from slot to capture by value
                TypeRef varType = sema_.lookupVarType(cap.name);
                info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                info.semType = varType;
                info.value = loadFromSlot(cap.name, info.type);
                info.isSlot = true;
            }
            else
            {
                auto localIt = locals_.find(cap.name);
                if (localIt != locals_.end())
                {
                    info.value = localIt->second;
                    TypeRef varType = sema_.lookupVarType(cap.name);
                    info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                    info.semType = varType;
                }
                else
                {
                    // Not found - might be a global or error
                    info.value = Value::constInt(0);
                    info.type = Type(Type::Kind::I64);
                    info.semType = types::unknown();
                }
            }
            captureInfos.push_back(info);
        }
    }

    // Save current function context (use index instead of pointer to handle vector reallocation)
    TypeRef savedReturnType = currentReturnType_;
    size_t savedFuncIdx = static_cast<size_t>(-1);
    if (currentFunc_)
    {
        for (size_t i = 0; i < module_->functions.size(); ++i)
        {
            if (&module_->functions[i] == currentFunc_)
            {
                savedFuncIdx = i;
                break;
            }
        }
    }
    size_t savedBlockIdx = blockMgr_.currentBlockIndex();
    unsigned savedNextBlockId = blockMgr_.nextBlockId();
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();

    // Create the lambda function and entry block via IRBuilder so param IDs are assigned.
    currentFunc_ = &builder_->startFunction(lambdaName, ilReturnType, params);
    currentReturnType_ = returnType;
    definedFunctions_.insert(lambdaName);

    blockMgr_.bind(builder_.get(), currentFunc_);

    // Create entry block with the lambda's params as block params.
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    const size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Load captured variables from the environment struct if we have captures
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    // First parameter is always __env (may be null for no-capture lambdas)
    if (hasCaptures)
    {
        Value envPtr = Value::temp(blockParams[0].id);

        // Load each captured variable from the environment
        size_t offset = 0;
        for (size_t i = 0; i < captureInfos.size(); ++i)
        {
            const auto &info = captureInfos[i];

            // GEP to get field address within env struct
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));

            // Load the captured value
            Value capturedVal = emitLoad(fieldAddr, info.type);

            // Create a slot for mutable captured variables
            createSlot(info.name, info.type);
            storeToSlot(info.name, capturedVal, info.type);
            localTypes_[info.name] = info.semType ? info.semType : types::unknown();

            // Advance offset by the size of this type
            offset += getILTypeSize(info.type);
        }
    }

    // Define user parameters as locals (skip __env at index 0)
    for (size_t i = 0; i < expr->params.size(); ++i)
    {
        size_t paramIdx = i + 1; // Skip __env
        if (paramIdx < blockParams.size())
        {
            TypeRef paramType = expr->params[i].type ? sema_.resolveType(expr->params[i].type.get())
                                                     : types::unknown();
            Type ilParamType = mapType(paramType);
            createSlot(expr->params[i].name, ilParamType);
            storeToSlot(expr->params[i].name, Value::temp(blockParams[paramIdx].id), ilParamType);
            localTypes_[expr->params[i].name] = paramType;
        }
    }

    // Lower the body - handle both block expressions and simple expressions
    LowerResult bodyResult{Value::constInt(0), Type(Type::Kind::Void)};
    if (auto *blockExpr = dynamic_cast<BlockExpr *>(expr->body.get()))
    {
        // Lower each statement in the block
        for (auto &stmt : blockExpr->statements)
        {
            lowerStmt(stmt.get());
        }
        // The block may have a final value expression
        if (blockExpr->value)
        {
            bodyResult = lowerExpr(blockExpr->value.get());
        }
    }
    else
    {
        bodyResult = lowerExpr(expr->body.get());
    }

    // Return the body result
    if (ilReturnType.kind == Type::Kind::Void)
    {
        if (!blockMgr_.isTerminated())
        {
            emitRetVoid();
        }
    }
    else
    {
        if (!blockMgr_.isTerminated())
        {
            Value returnValue = bodyResult.value;
            if (returnType && returnType->kind == TypeKindSem::Optional)
            {
                TypeRef bodyType = sema_.typeOf(expr->body.get());
                if (!bodyType || bodyType->kind != TypeKindSem::Optional)
                {
                    TypeRef innerType = returnType->innerType();
                    if (innerType)
                        returnValue = emitOptionalWrap(bodyResult.value, innerType);
                }
            }
            emitRet(returnValue);
        }
    }

    // Restore context (use saved index to get fresh pointer after potential vector reallocation)
    if (savedFuncIdx != static_cast<size_t>(-1))
    {
        currentFunc_ = &module_->functions[savedFuncIdx];
        blockMgr_.reset(currentFunc_);
        blockMgr_.setNextBlockId(savedNextBlockId);
        blockMgr_.setBlock(savedBlockIdx);
    }
    else
    {
        currentFunc_ = nullptr;
    }
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
    currentReturnType_ = savedReturnType;

    // Get the function pointer
    Value funcPtr = Value::global(lambdaName);

    // Always create a uniform closure struct: { funcPtr, envPtr }
    // For no-capture lambdas, envPtr is null

    // Allocate environment if we have captures
    Value envPtr = Value::constInt(0); // null for no captures
    if (hasCaptures)
    {
        size_t envSize = 0;
        for (const auto &info : captureInfos)
        {
            envSize += getILTypeSize(info.type);
        }

        // Allocate environment struct using rt_alloc (classId=0 for closures)
        Value classIdVal = Value::constInt(0);
        Value envSizeVal = Value::constInt(static_cast<int64_t>(envSize));
        envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {classIdVal, envSizeVal});

        // Store captured values into the environment
        size_t offset = 0;
        for (const auto &info : captureInfos)
        {
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));
            emitStore(fieldAddr, info.value, info.type);
            offset += getILTypeSize(info.type);
        }
    }

    // Allocate closure struct: { ptr funcPtr, ptr envPtr } = 16 bytes
    Value closureClassId = Value::constInt(0);
    Value closureSizeVal = Value::constInt(16);
    Value closurePtr =
        emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureClassId, closureSizeVal});

    // Store function pointer at offset 0
    emitStore(closurePtr, funcPtr, Type(Type::Kind::Ptr));

    // Store environment pointer at offset 8 (null for no captures)
    Value envFieldAddr = emitGEP(closurePtr, 8);
    emitStore(envFieldAddr, envPtr, Type(Type::Kind::Ptr));

    return {closurePtr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerMethodCall(MethodDecl *method,
                                     const std::string &typeName,
                                     Value selfValue,
                                     CallExpr *expr)
{
    // Lower method arguments
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue); // self is first argument

    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        if (i < method->params.size() && method->params[i].type)
        {
            TypeRef paramType = sema_.resolveType(method->params[i].type.get());
            TypeRef argType = sema_.typeOf(arg.value.get());
            if (paramType && paramType->kind == TypeKindSem::Optional)
            {
                TypeRef innerType = paramType->innerType();
                if (argType && argType->kind == TypeKindSem::Optional)
                {
                    argValue = result.value;
                }
                else if (argType && argType->kind == TypeKindSem::Unit)
                {
                    argValue = Value::null();
                }
                else if (innerType)
                {
                    argValue = emitOptionalWrap(result.value, innerType);
                }
            }
        }

        args.push_back(argValue);
    }

    // Get method return type
    TypeRef returnType =
        method->returnType ? sema_.resolveType(method->returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Call the method: TypeName.methodName
    std::string methodName = typeName + "." + method->name;

    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCall(methodName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallRet(ilReturnType, methodName, args);
        return {result, ilReturnType};
    }
}

LowerResult Lowerer::lowerBlockExpr(BlockExpr *expr)
{
    // Lower each statement in the block
    for (auto &stmt : expr->statements)
    {
        lowerStmt(stmt.get());
    }

    // If there's a trailing value expression, lower it and return
    if (expr->value)
    {
        return lowerExpr(expr->value.get());
    }

    // No value expression - return void/unit
    return {Value::constInt(0), Type(Type::Kind::Void)};
}

Lowerer::PatternValue Lowerer::emitTupleElement(const PatternValue &tuple,
                                                size_t index,
                                                TypeRef elemType)
{
    Type ilType = mapType(elemType);
    size_t offset = index * 8;
    Value elemPtr = tuple.value;
    if (offset > 0)
    {
        elemPtr = emitGEP(tuple.value, static_cast<int64_t>(offset));
    }
    Value elemVal = emitLoad(elemPtr, ilType);
    return {elemVal, elemType};
}

void Lowerer::emitPatternTest(const MatchArm::Pattern &pattern,
                              const PatternValue &scrutinee,
                              size_t successBlock,
                              size_t failureBlock)
{
    switch (pattern.kind)
    {
        case MatchArm::Pattern::Kind::Wildcard:
        case MatchArm::Pattern::Kind::Binding:
            emitBr(successBlock);
            return;

        case MatchArm::Pattern::Kind::Literal:
        {
            if (!pattern.literal)
            {
                emitBr(failureBlock);
                return;
            }
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                auto emitPtrCompare = [&](Opcode op) -> Value
                {
                    unsigned ptrSlotId = nextTempId();
                    il::core::Instr ptrSlotInstr;
                    ptrSlotInstr.result = ptrSlotId;
                    ptrSlotInstr.op = Opcode::Alloca;
                    ptrSlotInstr.type = Type(Type::Kind::Ptr);
                    ptrSlotInstr.operands = {Value::constInt(8)};
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
                    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
                    Value ptrAsI64 = Value::temp(ptrAsI64Id);

                    return emitBinary(op, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
                };

                if (pattern.literal->kind == ExprKind::NullLiteral)
                {
                    Value isNull = emitPtrCompare(Opcode::ICmpEq);
                    emitCBr(isNull, successBlock, failureBlock);
                    return;
                }

                Value isNotNull = emitPtrCompare(Opcode::ICmpNe);
                size_t someBlock = createBlock("match_opt_lit");
                emitCBr(isNotNull, someBlock, failureBlock);
                setBlock(someBlock);

                TypeRef innerType = scrutinee.type->innerType();
                auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                PatternValue inner{innerValue.value, innerType};
                emitPatternTest(pattern, inner, successBlock, failureBlock);
                return;
            }
            auto litResult = lowerExpr(pattern.literal.get());
            Value cond;
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::String)
            {
                cond = emitCallRet(
                    Type(Type::Kind::I1), kStringEquals, {scrutinee.value, litResult.value});
            }
            else if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Number)
            {
                cond = emitBinary(
                    Opcode::FCmpEQ, Type(Type::Kind::I1), scrutinee.value, litResult.value);
            }
            else
            {
                cond = emitBinary(
                    Opcode::ICmpEq, Type(Type::Kind::I1), scrutinee.value, litResult.value);
            }
            emitCBr(cond, successBlock, failureBlock);
            return;
        }

        case MatchArm::Pattern::Kind::Expression:
        {
            if (!pattern.literal)
            {
                emitBr(failureBlock);
                return;
            }
            auto exprResult = lowerExpr(pattern.literal.get());
            Value cond = exprResult.value;
            if (exprResult.type.kind != Type::Kind::I1)
            {
                cond = emitBinary(
                    Opcode::ICmpNe, Type(Type::Kind::I1), exprResult.value, Value::constInt(0));
            }
            emitCBr(cond, successBlock, failureBlock);
            return;
        }

        case MatchArm::Pattern::Kind::Tuple:
        {
            if (!scrutinee.type || scrutinee.type->kind != TypeKindSem::Tuple)
            {
                emitBr(failureBlock);
                return;
            }

            const auto &elements = scrutinee.type->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size())
            {
                emitBr(failureBlock);
                return;
            }

            for (size_t i = 0; i < elements.size(); ++i)
            {
                size_t nextBlock = (i + 1 < elements.size())
                                       ? createBlock("match_tuple_" + std::to_string(i))
                                       : successBlock;
                PatternValue elemValue = emitTupleElement(scrutinee, i, elements[i]);
                emitPatternTest(pattern.subpatterns[i], elemValue, nextBlock, failureBlock);
                if (i + 1 < elements.size())
                {
                    setBlock(nextBlock);
                }
            }
            return;
        }

        case MatchArm::Pattern::Kind::Constructor:
        {
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                auto emitPtrCompare = [&](Opcode op) -> Value
                {
                    unsigned ptrSlotId = nextTempId();
                    il::core::Instr ptrSlotInstr;
                    ptrSlotInstr.result = ptrSlotId;
                    ptrSlotInstr.op = Opcode::Alloca;
                    ptrSlotInstr.type = Type(Type::Kind::Ptr);
                    ptrSlotInstr.operands = {Value::constInt(8)};
                    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
                    Value ptrSlot = Value::temp(ptrSlotId);

                    il::core::Instr storePtrInstr;
                    storePtrInstr.op = Opcode::Store;
                    storePtrInstr.type = Type(Type::Kind::Ptr);
                    storePtrInstr.operands = {ptrSlot, scrutinee.value};
                    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

                    unsigned ptrAsI64Id = nextTempId();
                    il::core::Instr loadAsI64Instr;
                    loadAsI64Instr.result = ptrAsI64Id;
                    loadAsI64Instr.op = Opcode::Load;
                    loadAsI64Instr.type = Type(Type::Kind::I64);
                    loadAsI64Instr.operands = {ptrSlot};
                    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
                    Value ptrAsI64 = Value::temp(ptrAsI64Id);

                    return emitBinary(op, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
                };

                if (pattern.binding == "None")
                {
                    Value isNull = emitPtrCompare(Opcode::ICmpEq);
                    emitCBr(isNull, successBlock, failureBlock);
                    return;
                }

                if (pattern.binding == "Some")
                {
                    if (pattern.subpatterns.empty())
                    {
                        emitBr(failureBlock);
                        return;
                    }
                    Value isNotNull = emitPtrCompare(Opcode::ICmpNe);
                    size_t someBlock = createBlock("match_some");
                    emitCBr(isNotNull, someBlock, failureBlock);
                    setBlock(someBlock);

                    TypeRef innerType = scrutinee.type->innerType();
                    auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                    PatternValue inner{innerValue.value, innerType};
                    emitPatternTest(pattern.subpatterns[0], inner, successBlock, failureBlock);
                    return;
                }

                emitBr(failureBlock);
                return;
            }

            if (!scrutinee.type)
            {
                emitBr(failureBlock);
                return;
            }

            const std::vector<FieldLayout> *fields = nullptr;
            if (scrutinee.type->kind == TypeKindSem::Value)
            {
                auto it = valueTypes_.find(scrutinee.type->name);
                if (it != valueTypes_.end())
                    fields = &it->second.fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                auto it = entityTypes_.find(scrutinee.type->name);
                if (it != entityTypes_.end())
                    fields = &it->second.fields;
            }

            if (!fields || fields->size() != pattern.subpatterns.size())
            {
                emitBr(failureBlock);
                return;
            }

            for (size_t i = 0; i < fields->size(); ++i)
            {
                const FieldLayout &field = (*fields)[i];
                PatternValue fieldValue{emitFieldLoad(&field, scrutinee.value), field.type};
                size_t nextBlock = (i + 1 < fields->size())
                                       ? createBlock("match_ctor_" + std::to_string(i))
                                       : successBlock;
                emitPatternTest(pattern.subpatterns[i], fieldValue, nextBlock, failureBlock);
                if (i + 1 < fields->size())
                {
                    setBlock(nextBlock);
                }
            }
            return;
        }
    }
}

void Lowerer::emitPatternBindings(const MatchArm::Pattern &pattern, const PatternValue &scrutinee)
{
    switch (pattern.kind)
    {
        case MatchArm::Pattern::Kind::Binding:
            defineLocal(pattern.binding, scrutinee.value);
            if (scrutinee.type)
                localTypes_[pattern.binding] = scrutinee.type;
            return;

        case MatchArm::Pattern::Kind::Tuple:
        {
            if (!scrutinee.type || scrutinee.type->kind != TypeKindSem::Tuple)
                return;
            const auto &elements = scrutinee.type->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size())
                return;
            for (size_t i = 0; i < elements.size(); ++i)
            {
                PatternValue elemValue = emitTupleElement(scrutinee, i, elements[i]);
                emitPatternBindings(pattern.subpatterns[i], elemValue);
            }
            return;
        }

        case MatchArm::Pattern::Kind::Constructor:
        {
            if (scrutinee.type && scrutinee.type->kind == TypeKindSem::Optional)
            {
                if (pattern.binding != "Some" || pattern.subpatterns.empty())
                    return;
                TypeRef innerType = scrutinee.type->innerType();
                auto innerValue = emitOptionalUnwrap(scrutinee.value, innerType);
                PatternValue inner{innerValue.value, innerType};
                emitPatternBindings(pattern.subpatterns[0], inner);
                return;
            }

            if (!scrutinee.type)
                return;

            const std::vector<FieldLayout> *fields = nullptr;
            if (scrutinee.type->kind == TypeKindSem::Value)
            {
                auto it = valueTypes_.find(scrutinee.type->name);
                if (it != valueTypes_.end())
                    fields = &it->second.fields;
            }
            else if (scrutinee.type->kind == TypeKindSem::Entity)
            {
                auto it = entityTypes_.find(scrutinee.type->name);
                if (it != entityTypes_.end())
                    fields = &it->second.fields;
            }

            if (!fields || fields->size() != pattern.subpatterns.size())
                return;

            for (size_t i = 0; i < fields->size(); ++i)
            {
                const FieldLayout &field = (*fields)[i];
                PatternValue fieldValue{emitFieldLoad(&field, scrutinee.value), field.type};
                emitPatternBindings(pattern.subpatterns[i], fieldValue);
            }
            return;
        }

        default:
            return;
    }
}

LowerResult Lowerer::lowerMatchExpr(MatchExpr *expr)
{
    if (expr->arms.empty())
    {
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }

    // Lower the scrutinee once and store in a slot for reuse
    auto scrutinee = lowerExpr(expr->scrutinee.get());
    std::string scrutineeSlot = "__match_scrutinee";
    createSlot(scrutineeSlot, scrutinee.type);
    storeToSlot(scrutineeSlot, scrutinee.value, scrutinee.type);
    TypeRef scrutineeType = sema_.typeOf(expr->scrutinee.get());

    // Determine the result type from the first arm body
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Create a result slot to store the match result
    std::string resultSlot = "__match_result";
    bool hasResult = ilResultType.kind != Type::Kind::Void;
    if (hasResult)
        createSlot(resultSlot, ilResultType);

    // Create end block for the match
    size_t endIdx = createBlock("match_end");

    // Create blocks for each arm body and the next arm's test
    std::vector<size_t> armBlocks;
    std::vector<size_t> nextTestBlocks;
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        armBlocks.push_back(createBlock("match_arm_" + std::to_string(i)));
        if (i + 1 < expr->arms.size())
        {
            nextTestBlocks.push_back(createBlock("match_test_" + std::to_string(i + 1)));
        }
        else
        {
            nextTestBlocks.push_back(endIdx); // Last arm falls through to end
        }
    }

    // Lower each arm
    for (size_t i = 0; i < expr->arms.size(); ++i)
    {
        const auto &arm = expr->arms[i];
        auto localsBackup = locals_;
        auto slotsBackup = slots_;
        auto localTypesBackup = localTypes_;

        size_t matchBlock = armBlocks[i];
        size_t guardBlock = 0;
        if (arm.pattern.guard)
        {
            guardBlock = createBlock("match_guard_" + std::to_string(i));
            matchBlock = guardBlock;
        }

        // In the current block, test the pattern
        Value scrutineeVal = loadFromSlot(scrutineeSlot, scrutinee.type);
        PatternValue scrutineeValue{scrutineeVal, scrutineeType};
        emitPatternTest(arm.pattern, scrutineeValue, matchBlock, nextTestBlocks[i]);

        if (guardBlock)
        {
            setBlock(guardBlock);
            emitPatternBindings(arm.pattern, scrutineeValue);
            auto guardResult = lowerExpr(arm.pattern.guard.get());
            emitCBr(guardResult.value, armBlocks[i], nextTestBlocks[i]);
        }

        // Lower the arm body and store result
        setBlock(armBlocks[i]);
        if (!guardBlock)
            emitPatternBindings(arm.pattern, scrutineeValue);
        if (arm.body)
        {
            auto bodyResult = lowerExpr(arm.body.get());
            if (hasResult)
            {
                Value bodyValue = bodyResult.value;
                if (expectsOptional)
                {
                    TypeRef bodyType = sema_.typeOf(arm.body.get());
                    if (!bodyType || bodyType->kind != TypeKindSem::Optional)
                    {
                        if (optionalInner)
                            bodyValue = emitOptionalWrap(bodyResult.value, optionalInner);
                    }
                }
                storeToSlot(resultSlot, bodyValue, ilResultType);
            }
        }

        // Jump to end after arm body (if not already terminated)
        if (!isTerminated())
        {
            emitBr(endIdx);
        }

        locals_ = std::move(localsBackup);
        slots_ = std::move(slotsBackup);
        localTypes_ = std::move(localTypesBackup);

        // Set up next test block for pattern matching
        if (i + 1 < expr->arms.size())
        {
            setBlock(nextTestBlocks[i]);
        }
    }

    // Remove the scrutinee slot
    removeSlot(scrutineeSlot);

    // Continue from end block
    setBlock(endIdx);

    // Load and return the result
    if (hasResult)
    {
        Value result = loadFromSlot(resultSlot, ilResultType);
        removeSlot(resultSlot);
        return {result, ilResultType};
    }

    return {Value::constInt(0), Type(Type::Kind::Void)};
}


} // namespace il::frontends::viperlang
