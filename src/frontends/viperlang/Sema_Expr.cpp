//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr.cpp
/// @brief Expression analysis for the ViperLang semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"

namespace il::frontends::viperlang
{

//=============================================================================
// Expression Analysis
//=============================================================================

TypeRef Sema::analyzeExpr(Expr *expr)
{
    if (!expr)
        return types::unknown();

    TypeRef result;

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            result = analyzeIntLiteral(static_cast<IntLiteralExpr *>(expr));
            break;
        case ExprKind::NumberLiteral:
            result = analyzeNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
            break;
        case ExprKind::StringLiteral:
            result = analyzeStringLiteral(static_cast<StringLiteralExpr *>(expr));
            break;
        case ExprKind::BoolLiteral:
            result = analyzeBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
            break;
        case ExprKind::NullLiteral:
            result = analyzeNullLiteral(static_cast<NullLiteralExpr *>(expr));
            break;
        case ExprKind::UnitLiteral:
            result = analyzeUnitLiteral(static_cast<UnitLiteralExpr *>(expr));
            break;
        case ExprKind::Ident:
            result = analyzeIdent(static_cast<IdentExpr *>(expr));
            break;
        case ExprKind::SelfExpr:
            result = analyzeSelf(static_cast<SelfExpr *>(expr));
            break;
        case ExprKind::Binary:
            result = analyzeBinary(static_cast<BinaryExpr *>(expr));
            break;
        case ExprKind::Unary:
            result = analyzeUnary(static_cast<UnaryExpr *>(expr));
            break;
        case ExprKind::Ternary:
            result = analyzeTernary(static_cast<TernaryExpr *>(expr));
            break;
        case ExprKind::Call:
            result = analyzeCall(static_cast<CallExpr *>(expr));
            break;
        case ExprKind::Index:
            result = analyzeIndex(static_cast<IndexExpr *>(expr));
            break;
        case ExprKind::Field:
            result = analyzeField(static_cast<FieldExpr *>(expr));
            break;
        case ExprKind::OptionalChain:
            result = analyzeOptionalChain(static_cast<OptionalChainExpr *>(expr));
            break;
        case ExprKind::Coalesce:
            result = analyzeCoalesce(static_cast<CoalesceExpr *>(expr));
            break;
        case ExprKind::Is:
            result = analyzeIs(static_cast<IsExpr *>(expr));
            break;
        case ExprKind::As:
            result = analyzeAs(static_cast<AsExpr *>(expr));
            break;
        case ExprKind::Range:
            result = analyzeRange(static_cast<RangeExpr *>(expr));
            break;
        case ExprKind::New:
            result = analyzeNew(static_cast<NewExpr *>(expr));
            break;
        case ExprKind::Lambda:
            result = analyzeLambda(static_cast<LambdaExpr *>(expr));
            break;
        case ExprKind::Match:
            result = analyzeMatchExpr(static_cast<MatchExpr *>(expr));
            break;
        case ExprKind::ListLiteral:
            result = analyzeListLiteral(static_cast<ListLiteralExpr *>(expr));
            break;
        case ExprKind::MapLiteral:
            result = analyzeMapLiteral(static_cast<MapLiteralExpr *>(expr));
            break;
        case ExprKind::SetLiteral:
            result = analyzeSetLiteral(static_cast<SetLiteralExpr *>(expr));
            break;
        case ExprKind::Tuple:
            result = analyzeTuple(static_cast<TupleExpr *>(expr));
            break;
        case ExprKind::TupleIndex:
            result = analyzeTupleIndex(static_cast<TupleIndexExpr *>(expr));
            break;
        default:
            result = types::unknown();
            break;
    }

    exprTypes_[expr] = result;
    return result;
}

TypeRef Sema::analyzeIntLiteral(IntLiteralExpr * /*expr*/)
{
    return types::integer();
}

TypeRef Sema::analyzeNumberLiteral(NumberLiteralExpr * /*expr*/)
{
    return types::number();
}

TypeRef Sema::analyzeStringLiteral(StringLiteralExpr * /*expr*/)
{
    return types::string();
}

TypeRef Sema::analyzeBoolLiteral(BoolLiteralExpr * /*expr*/)
{
    return types::boolean();
}

TypeRef Sema::analyzeNullLiteral(NullLiteralExpr * /*expr*/)
{
    // null is Optional[Unknown] - needs context to determine actual type
    return types::optional(types::unknown());
}

TypeRef Sema::analyzeUnitLiteral(UnitLiteralExpr * /*expr*/)
{
    return types::unit();
}

TypeRef Sema::analyzeIdent(IdentExpr *expr)
{
    Symbol *sym = lookupSymbol(expr->name);
    if (!sym)
    {
        errorUndefined(expr->loc, expr->name);
        return types::unknown();
    }
    return sym->type;
}

TypeRef Sema::analyzeSelf(SelfExpr *expr)
{
    if (!currentSelfType_)
    {
        error(expr->loc, "'self' can only be used inside a method");
        return types::unknown();
    }
    return currentSelfType_;
}

TypeRef Sema::analyzeBinary(BinaryExpr *expr)
{
    TypeRef leftType = analyzeExpr(expr->left.get());
    TypeRef rightType = analyzeExpr(expr->right.get());

    switch (expr->op)
    {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            // Numeric operations
            if (leftType->kind == TypeKindSem::String && expr->op == BinaryOp::Add)
            {
                // String concatenation
                return types::string();
            }
            if (leftType->isNumeric() && rightType->isNumeric())
            {
                // Return wider type
                if (leftType->kind == TypeKindSem::Number || rightType->kind == TypeKindSem::Number)
                    return types::number();
                return types::integer();
            }
            error(expr->loc, "Invalid operands for arithmetic operation");
            return types::unknown();

        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
            // Comparison operations
            return types::boolean();

        case BinaryOp::And:
        case BinaryOp::Or:
            // Logical operations
            if (leftType->kind != TypeKindSem::Boolean || rightType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical operators require Boolean operands");
            }
            return types::boolean();

        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
            // Bitwise operations
            if (!leftType->isIntegral() || !rightType->isIntegral())
            {
                error(expr->loc, "Bitwise operators require integral operands");
            }
            return types::integer();

        case BinaryOp::Assign:
            // Assignment - LHS must be assignable, types must be compatible
            // For now, just check that the types are compatible
            if (!rightType->isConvertibleTo(*leftType))
            {
                errorTypeMismatch(expr->loc, leftType, rightType);
            }
            // Assignment expression returns the assigned value
            return leftType;
    }

    return types::unknown();
}

TypeRef Sema::analyzeUnary(UnaryExpr *expr)
{
    TypeRef operandType = analyzeExpr(expr->operand.get());

    switch (expr->op)
    {
        case UnaryOp::Neg:
            if (!operandType->isNumeric())
            {
                error(expr->loc, "Negation requires numeric operand");
            }
            return operandType;

        case UnaryOp::Not:
            if (operandType->kind != TypeKindSem::Boolean)
            {
                error(expr->loc, "Logical not requires Boolean operand");
            }
            return types::boolean();

        case UnaryOp::BitNot:
            if (!operandType->isIntegral())
            {
                error(expr->loc, "Bitwise not requires integral operand");
            }
            return types::integer();
    }

    return types::unknown();
}

TypeRef Sema::analyzeTernary(TernaryExpr *expr)
{
    TypeRef condType = analyzeExpr(expr->condition.get());
    if (condType->kind != TypeKindSem::Boolean)
    {
        error(expr->condition->loc, "Condition must be Boolean");
    }

    TypeRef thenType = analyzeExpr(expr->thenExpr.get());
    TypeRef elseType = analyzeExpr(expr->elseExpr.get());

    // TODO: Compute common type
    if (thenType->equals(*elseType))
        return thenType;
    if (thenType->isAssignableFrom(*elseType))
        return thenType;
    if (elseType->isAssignableFrom(*thenType))
        return elseType;

    error(expr->loc, "Incompatible types in ternary expression");
    return types::unknown();
}

/// @brief Try to extract a dotted name from a field access chain.
/// @param expr The expression to extract from.
/// @param out The output string to append to.
/// @return True if successful, false otherwise.
static bool extractDottedName(Expr *expr, std::string &out)
{
    if (!expr)
        return false;
    if (expr->kind == ExprKind::Ident)
    {
        auto *ident = static_cast<IdentExpr *>(expr);
        out = ident->name;
        return true;
    }
    if (expr->kind == ExprKind::Field)
    {
        auto *fieldExpr = static_cast<FieldExpr *>(expr);
        if (!extractDottedName(fieldExpr->base.get(), out))
            return false;
        out += ".";
        out += fieldExpr->field;
        return true;
    }
    return false;
}

TypeRef Sema::analyzeCall(CallExpr *expr)
{
    // First, try to resolve dotted runtime function names like Viper.Terminal.Say
    std::string dottedName;
    if (extractDottedName(expr->callee.get(), dottedName))
    {
        // Check if it's a known runtime function
        auto it = runtimeFunctions_.find(dottedName);
        if (it != runtimeFunctions_.end())
        {
            // Analyze arguments
            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }
            // Store the resolved runtime call info
            runtimeCallees_[expr] = dottedName;
            return it->second;
        }
    }

    // Handle special built-in method calls on collections
    // This allows list.count() as an alternative to list.count
    if (expr->callee->kind == ExprKind::Field)
    {
        auto *fieldExpr = static_cast<FieldExpr *>(expr->callee.get());
        TypeRef baseType = analyzeExpr(fieldExpr->base.get());

        // Handle List methods
        if (baseType && baseType->kind == TypeKindSem::List)
        {
            if (fieldExpr->field == "count" || fieldExpr->field == "size" ||
                fieldExpr->field == "length")
            {
                // Analyze arguments (should be empty)
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::integer();
            }
            if (fieldExpr->field == "isEmpty")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::boolean();
            }
        }

        // Handle String methods
        if (baseType && baseType->kind == TypeKindSem::String)
        {
            if (fieldExpr->field == "length" || fieldExpr->field == "count" ||
                fieldExpr->field == "size")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::integer();
            }
            if (fieldExpr->field == "isEmpty")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::boolean();
            }
        }
    }

    TypeRef calleeType = analyzeExpr(expr->callee.get());

    // Analyze arguments
    for (auto &arg : expr->args)
    {
        analyzeExpr(arg.value.get());
    }

    // If callee is a function type, return its return type
    if (calleeType->kind == TypeKindSem::Function)
    {
        return calleeType->returnType();
    }

    // If callee is unknown, return unknown
    if (calleeType->kind == TypeKindSem::Unknown)
    {
        return types::unknown();
    }

    // Could be a constructor call (Type(args))
    if (calleeType->kind == TypeKindSem::Value || calleeType->kind == TypeKindSem::Entity)
    {
        return calleeType;
    }

    error(expr->loc, "Expression is not callable");
    return types::unknown();
}

TypeRef Sema::analyzeIndex(IndexExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());
    TypeRef indexType = analyzeExpr(expr->index.get());

    if (baseType->kind == TypeKindSem::List || baseType->kind == TypeKindSem::String)
    {
        if (!indexType->isIntegral())
        {
            error(expr->index->loc, "Index must be an integer");
        }
        if (baseType->kind == TypeKindSem::String)
            return types::string();
        return baseType->elementType() ? baseType->elementType() : types::unknown();
    }

    if (baseType->kind == TypeKindSem::Map)
    {
        return baseType->valueType() ? baseType->valueType() : types::unknown();
    }

    error(expr->loc, "Expression is not indexable");
    return types::unknown();
}

TypeRef Sema::analyzeField(FieldExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());

    // Check if this is a field or method access on a value or entity type
    if (baseType && (baseType->kind == TypeKindSem::Value || baseType->kind == TypeKindSem::Entity))
    {
        std::string memberKey = baseType->name + "." + expr->field;

        // Check if accessing from inside or outside the type
        bool isInsideType = currentSelfType_ && currentSelfType_->name == baseType->name;

        // Check visibility
        auto visIt = memberVisibility_.find(memberKey);
        if (visIt != memberVisibility_.end())
        {
            if (visIt->second == Visibility::Private && !isInsideType)
            {
                error(expr->loc,
                      "Cannot access private member '" + expr->field + "' of type '" +
                          baseType->name + "'");
            }
        }

        // Check if it's a method
        auto methodIt = methodTypes_.find(memberKey);
        if (methodIt != methodTypes_.end())
        {
            return methodIt->second;
        }

        // Check if it's a field
        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end())
        {
            return fieldIt->second;
        }
    }

    // Handle built-in properties like .count on lists
    if (baseType && baseType->kind == TypeKindSem::List)
    {
        if (expr->field == "count" || expr->field == "size")
        {
            return types::integer();
        }
    }

    return types::unknown();
}

TypeRef Sema::analyzeOptionalChain(OptionalChainExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());

    // Result is always optional
    // TODO: Look up field type
    return types::optional(types::unknown());
}

TypeRef Sema::analyzeCoalesce(CoalesceExpr *expr)
{
    TypeRef leftType = analyzeExpr(expr->left.get());
    TypeRef rightType = analyzeExpr(expr->right.get());

    // Left should be optional
    if (leftType->kind != TypeKindSem::Optional)
    {
        error(expr->left->loc, "Left side of ?? must be optional");
    }

    // Result is the unwrapped type
    TypeRef innerType = leftType->innerType();
    return innerType ? innerType : rightType;
}

TypeRef Sema::analyzeIs(IsExpr *expr)
{
    analyzeExpr(expr->value.get());
    resolveTypeNode(expr->type.get());
    return types::boolean();
}

TypeRef Sema::analyzeAs(AsExpr *expr)
{
    analyzeExpr(expr->value.get());
    return resolveTypeNode(expr->type.get());
}

TypeRef Sema::analyzeRange(RangeExpr *expr)
{
    TypeRef startType = analyzeExpr(expr->start.get());
    TypeRef endType = analyzeExpr(expr->end.get());

    if (!startType->isIntegral() || !endType->isIntegral())
    {
        error(expr->loc, "Range bounds must be integers");
    }

    // Range type is internal - used for iteration
    return types::list(types::integer());
}

TypeRef Sema::analyzeMatchExpr(MatchExpr *expr)
{
    TypeRef scrutineeType = analyzeExpr(expr->scrutinee.get());

    // Track if we have a wildcard or exhaustive pattern coverage
    bool hasWildcard = false;
    std::set<int64_t> coveredIntegers;
    std::set<bool> coveredBooleans;

    TypeRef resultType = nullptr;

    for (auto &arm : expr->arms)
    {
        // Analyze the pattern
        const auto &pattern = arm.pattern;

        if (pattern.kind == MatchArm::Pattern::Kind::Wildcard)
        {
            hasWildcard = true;
        }
        else if (pattern.kind == MatchArm::Pattern::Kind::Binding)
        {
            // A binding without a guard acts as a wildcard
            if (!pattern.guard)
            {
                hasWildcard = true;
            }
        }
        else if (pattern.kind == MatchArm::Pattern::Kind::Literal && pattern.literal)
        {
            // Track which literals are covered
            if (pattern.literal->kind == ExprKind::IntLiteral)
            {
                coveredIntegers.insert(static_cast<IntLiteralExpr *>(pattern.literal.get())->value);
            }
            else if (pattern.literal->kind == ExprKind::BoolLiteral)
            {
                coveredBooleans.insert(
                    static_cast<BoolLiteralExpr *>(pattern.literal.get())->value);
            }
        }

        // Analyze the body and track result type
        TypeRef bodyType = analyzeExpr(arm.body.get());
        if (!resultType)
        {
            resultType = bodyType;
        }
        else if (bodyType && !resultType->equals(*bodyType))
        {
            // Types differ - for now, use the first type
            // TODO: Find common supertype
        }
    }

    // Check exhaustiveness based on scrutinee type
    if (!hasWildcard)
    {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean)
        {
            // Boolean must cover both true and false
            if (coveredBooleans.size() < 2)
            {
                error(expr->loc,
                      "Non-exhaustive patterns: match on Boolean must cover both true "
                      "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            // Integer types need a wildcard since we can't enumerate all values
            error(expr->loc,
                  "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                  "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            // Optional types need to handle both Some and None cases
            error(expr->loc,
                  "Non-exhaustive patterns: match on optional type should use a "
                  "wildcard (_) or handle all cases");
        }
    }

    return resultType ? resultType : types::unknown();
}

TypeRef Sema::analyzeNew(NewExpr *expr)
{
    TypeRef type = resolveTypeNode(expr->type.get());

    // Allow new for entity types and collection types (List, Set, Map)
    if (type->kind != TypeKindSem::Entity && type->kind != TypeKindSem::List &&
        type->kind != TypeKindSem::Set && type->kind != TypeKindSem::Map)
    {
        error(expr->loc, "'new' can only be used with entity or collection types");
    }

    // Analyze constructor arguments
    for (auto &arg : expr->args)
    {
        analyzeExpr(arg.value.get());
    }

    return type;
}

TypeRef Sema::analyzeLambda(LambdaExpr *expr)
{
    // Collect names that are local to the lambda (params)
    std::set<std::string> lambdaLocals;
    for (const auto &param : expr->params)
    {
        lambdaLocals.insert(param.name);
    }

    pushScope();

    std::vector<TypeRef> paramTypes;
    for (const auto &param : expr->params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
        paramTypes.push_back(paramType);

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym);
    }

    TypeRef bodyType = analyzeExpr(expr->body.get());

    popScope();

    // Collect captured variables (free variables referenced in the body)
    collectCaptures(expr->body.get(), lambdaLocals, expr->captures);

    TypeRef returnType = expr->returnType ? resolveTypeNode(expr->returnType.get()) : bodyType;
    return types::function(paramTypes, returnType);
}

TypeRef Sema::analyzeListLiteral(ListLiteralExpr *expr)
{
    TypeRef elementType = types::unknown();

    for (auto &elem : expr->elements)
    {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elementType->kind == TypeKindSem::Unknown)
        {
            elementType = elemType;
        }
        else if (!elementType->equals(*elemType))
        {
            // TODO: Find common type
        }
    }

    return types::list(elementType);
}

TypeRef Sema::analyzeMapLiteral(MapLiteralExpr *expr)
{
    TypeRef keyType = types::unknown();
    TypeRef valueType = types::unknown();

    for (auto &entry : expr->entries)
    {
        TypeRef kType = analyzeExpr(entry.key.get());
        TypeRef vType = analyzeExpr(entry.value.get());

        if (keyType->kind == TypeKindSem::Unknown)
            keyType = kType;
        if (valueType->kind == TypeKindSem::Unknown)
            valueType = vType;
    }

    return types::map(keyType, valueType);
}

TypeRef Sema::analyzeSetLiteral(SetLiteralExpr *expr)
{
    TypeRef elementType = types::unknown();

    for (auto &elem : expr->elements)
    {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elementType->kind == TypeKindSem::Unknown)
        {
            elementType = elemType;
        }
    }

    return types::set(elementType);
}

TypeRef Sema::analyzeTuple(TupleExpr *expr)
{
    std::vector<TypeRef> elementTypes;
    for (auto &elem : expr->elements)
    {
        elementTypes.push_back(analyzeExpr(elem.get()));
    }
    return types::tuple(std::move(elementTypes));
}

TypeRef Sema::analyzeTupleIndex(TupleIndexExpr *expr)
{
    TypeRef tupleType = analyzeExpr(expr->tuple.get());

    if (!tupleType->isTuple())
    {
        error(expr->loc,
              "tuple index access requires a tuple type, got '" + tupleType->toString() + "'");
        return types::unknown();
    }

    if (expr->index >= tupleType->tupleElementTypes().size())
    {
        error(expr->loc,
              "tuple index " + std::to_string(expr->index) + " is out of bounds for " +
                  tupleType->toString());
        return types::unknown();
    }

    return tupleType->tupleElementType(expr->index);
}


} // namespace il::frontends::viperlang
