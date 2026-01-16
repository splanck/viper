//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr.cpp
/// @brief Expression analysis for the Zia semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

namespace il::frontends::zia
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
        case ExprKind::Block:
            result = analyzeBlockExpr(static_cast<BlockExpr *>(expr));
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

    TypeRef resultType = commonType(thenType, elseType);
    if (resultType && resultType->kind != TypeKindSem::Unknown)
        return resultType;

    error(expr->loc, "Incompatible types in ternary expression");
    return types::unknown();
}

TypeRef Sema::commonType(TypeRef lhs, TypeRef rhs)
{
    if (!lhs && !rhs)
        return types::unknown();
    if (!lhs)
        return rhs;
    if (!rhs)
        return lhs;
    if (lhs->kind == TypeKindSem::Unknown)
        return rhs;
    if (rhs->kind == TypeKindSem::Unknown)
        return lhs;

    if (lhs->kind == TypeKindSem::Optional || rhs->kind == TypeKindSem::Optional)
    {
        TypeRef innerL = lhs->kind == TypeKindSem::Optional ? lhs->innerType() : lhs;
        TypeRef innerR = rhs->kind == TypeKindSem::Optional ? rhs->innerType() : rhs;
        TypeRef inner = commonType(innerL, innerR);
        return types::optional(inner ? inner : types::unknown());
    }

    if (lhs->isNumeric() && rhs->isNumeric())
    {
        if (lhs->kind == TypeKindSem::Number || rhs->kind == TypeKindSem::Number)
            return types::number();
        if (lhs->kind == TypeKindSem::Integer || rhs->kind == TypeKindSem::Integer)
            return types::integer();
        return types::byte();
    }

    if (lhs->isAssignableFrom(*rhs))
        return lhs;
    if (rhs->isAssignableFrom(*lhs))
        return rhs;

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
    // First, try to resolve dotted function names like Viper.Terminal.Say or MyLib.helper
    // This unified lookup works for both runtime functions and user-defined namespaced functions
    std::string dottedName;
    if (extractDottedName(expr->callee.get(), dottedName))
    {
        // Check if it's a known function (runtime or user-defined with qualified name)
        Symbol *sym = lookupSymbol(dottedName);
        if (sym && sym->kind == Symbol::Kind::Function)
        {
            // Bug #024 fix: Store the callee's type so the lowerer can access it
            // The lowerer uses sema_.typeOf(expr->callee.get()) to determine return type
            TypeRef funcType = sym->type;
            exprTypes_[expr->callee.get()] = funcType;

            // Analyze arguments
            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }
            // For extern functions (runtime library), store the resolved call info
            // so the lowerer knows to emit an extern call
            if (sym->isExtern)
            {
                runtimeCallees_[expr] = dottedName;
            }
            // Bug #023 fix: Return the function's return type, not the function type itself
            if (funcType && funcType->kind == TypeKindSem::Function)
            {
                return funcType->returnType();
            }

            // BUG-007 fix: For extern runtime class constructors, return proper collection types
            // This enables for-in iteration over runtime lists and maps
            // For extern functions, sym.type is the return type directly (not wrapped in Function)
            if (sym->isExtern && funcType && funcType->kind == TypeKindSem::Ptr &&
                !funcType->name.empty())
            {
                // Check if this is a List runtime class
                if (funcType->name == "Viper.Collections.List")
                {
                    // Return a List type with unknown element type
                    return types::list(types::unknown());
                }
                if (funcType->name == "Viper.Collections.Map")
                {
                    // Return a Map type with unknown key/value types
                    return types::map(types::unknown(), types::unknown());
                }
                if (funcType->name == "Viper.Collections.Set")
                {
                    // Return a Set type with unknown element type
                    return types::set(types::unknown());
                }
            }

            return funcType;
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
            TypeRef elemType = baseType->elementType();

            // Methods that return the element type
            if (fieldExpr->field == "get" || fieldExpr->field == "first" ||
                fieldExpr->field == "last" || fieldExpr->field == "pop")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return elemType ? elemType : types::unknown();
            }

            // Methods that return Integer
            if (fieldExpr->field == "count" || fieldExpr->field == "size" ||
                fieldExpr->field == "length" || fieldExpr->field == "indexOf" ||
                fieldExpr->field == "lastIndexOf")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::integer();
            }

            // Methods that return Boolean
            if (fieldExpr->field == "isEmpty" || fieldExpr->field == "contains" ||
                fieldExpr->field == "remove")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::boolean();
            }

            // Methods that return void
            if (fieldExpr->field == "add" || fieldExpr->field == "insert" ||
                fieldExpr->field == "set" || fieldExpr->field == "clear" ||
                fieldExpr->field == "reverse" || fieldExpr->field == "sort")
            {
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                return types::voidType();
            }
        }

        // Handle Map methods
        if (baseType && baseType->kind == TypeKindSem::Map)
        {
            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }

            auto requireStringKey = [&]()
            {
                if (expr->args.empty())
                    return;
                TypeRef keyType = analyzeExpr(expr->args[0].value.get());
                if (keyType && keyType->kind != TypeKindSem::String &&
                    keyType->kind != TypeKindSem::Unknown)
                {
                    error(expr->args[0].value->loc, "Map keys must be String");
                }
            };

            if (fieldExpr->field == "get" || fieldExpr->field == "getOr")
            {
                requireStringKey();
                return baseType->valueType() ? baseType->valueType() : types::unknown();
            }
            if (fieldExpr->field == "set" || fieldExpr->field == "put")
            {
                requireStringKey();
                return types::voidType();
            }
            if (fieldExpr->field == "setIfMissing")
            {
                requireStringKey();
                return types::boolean();
            }
            if (fieldExpr->field == "containsKey" || fieldExpr->field == "hasKey" ||
                fieldExpr->field == "has")
            {
                requireStringKey();
                return types::boolean();
            }
            if (fieldExpr->field == "remove")
            {
                requireStringKey();
                return types::boolean();
            }
            if (fieldExpr->field == "size" || fieldExpr->field == "count" ||
                fieldExpr->field == "length")
            {
                return types::integer();
            }
            if (fieldExpr->field == "clear")
            {
                return types::voidType();
            }
            if (fieldExpr->field == "keys" || fieldExpr->field == "values")
            {
                return types::unknown();
            }
        }

        // Handle Set methods
        if (baseType && baseType->kind == TypeKindSem::Set)
        {
            TypeRef elemType = baseType->elementType();

            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }

            // Methods that return Boolean
            if (fieldExpr->field == "contains" || fieldExpr->field == "has" ||
                fieldExpr->field == "add" || fieldExpr->field == "remove")
            {
                return types::boolean();
            }

            // Methods that return Integer
            if (fieldExpr->field == "size" || fieldExpr->field == "count" ||
                fieldExpr->field == "length")
            {
                return types::integer();
            }

            // Methods that return void
            if (fieldExpr->field == "clear")
            {
                return types::voidType();
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

        // Handle runtime class method calls (e.g., canvas.Poll(), canvas.Clear())
        // Runtime classes have names starting with "Viper." and are registered in typeRegistry_
        if (baseType && baseType->name.find("Viper.") == 0)
        {
            // Construct full method name: ClassName.MethodName
            std::string fullMethodName = baseType->name + "." + fieldExpr->field;

            // Check if it's a known function (runtime method)
            Symbol *sym = lookupSymbol(fullMethodName);

            // If not found and this is a GUI widget class, try falling back to Widget base class
            // This handles inherited methods like SetSize, AddChild, SetVisible, etc.
            if (!sym && baseType->name.find("Viper.GUI.") == 0 &&
                baseType->name != "Viper.GUI.Widget")
            {
                std::string widgetMethodName = "Viper.GUI.Widget." + fieldExpr->field;
                sym = lookupSymbol(widgetMethodName);
                if (sym && sym->kind == Symbol::Kind::Function)
                {
                    fullMethodName = widgetMethodName;
                }
            }

            if (sym && sym->kind == Symbol::Kind::Function)
            {
                // Analyze arguments
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }
                // Store the resolved runtime call info for the lowerer
                if (sym->isExtern)
                {
                    runtimeCallees_[expr] = fullMethodName;
                }
                return sym->type;
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
        if (indexType->kind != TypeKindSem::String)
        {
            error(expr->index->loc, "Map keys must be String");
        }
        return baseType->valueType() ? baseType->valueType() : types::unknown();
    }

    error(expr->loc, "Expression is not indexable");
    return types::unknown();
}

TypeRef Sema::analyzeField(FieldExpr *expr)
{
    // BUG-012 fix: Handle runtime class namespace property access (e.g., Viper.Math.Pi)
    // For property access like Viper.Math.Pi, we need to resolve it as a getter call
    // before trying to analyze the base, because "Viper" is not a symbol.
    std::string dottedBase;
    if (extractDottedName(expr->base.get(), dottedBase))
    {
        // Check if the dotted base is a runtime class (registered in typeRegistry_)
        auto typeIt = typeRegistry_.find(dottedBase);
        if (typeIt != typeRegistry_.end())
        {
            // Try to find a getter function: {ClassName}.get_{PropertyName}
            std::string getterName = dottedBase + ".get_" + expr->field;
            Symbol *sym = lookupSymbol(getterName);
            if (sym && sym->kind == Symbol::Kind::Function)
            {
                // Store the resolved getter for the lowerer
                runtimeFieldGetters_[expr] = getterName;
                // Return the function's return type (the property type)
                TypeRef funcType = sym->type;
                if (funcType && funcType->kind == TypeKindSem::Function)
                {
                    return funcType->returnType();
                }
                return funcType;
            }
        }
    }

    TypeRef baseType = analyzeExpr(expr->base.get());

    // Unwrap Optional types for field/method access
    // This handles variables assigned from optionals after null checks
    if (baseType && baseType->kind == TypeKindSem::Optional && baseType->innerType())
    {
        baseType = baseType->innerType();
    }

    // Handle module-qualified access (e.g., colors.initColors)
    if (baseType && baseType->kind == TypeKindSem::Module)
    {
        // Look up the symbol in global scope (imported symbols are merged)
        Symbol *sym = lookupSymbol(expr->field);
        if (sym)
        {
            return sym->type;
        }
        // If not found in global scope, report error
        error(expr->loc, "Module '" + baseType->name + "' has no exported symbol '" + expr->field + "'");
        return types::unknown();
    }

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
        if (expr->field == "Count" || expr->field == "count" || expr->field == "size" ||
            expr->field == "length")
        {
            return types::integer();
        }
    }

    // Handle built-in properties on strings (Bug #3 fix)
    if (baseType && baseType->kind == TypeKindSem::String)
    {
        if (expr->field == "Length" || expr->field == "length")
        {
            return types::integer();
        }
    }

    // Handle runtime class property access (e.g., app.Root, editor.LineCount)
    // Runtime classes are Ptr types with a name like "Viper.GUI.App"
    if (baseType && baseType->kind == TypeKindSem::Ptr && !baseType->name.empty() &&
        baseType->name.find("Viper.") == 0)
    {
        // Construct getter function name: {ClassName}.get_{PropertyName}
        std::string getterName = baseType->name + ".get_" + expr->field;

        // Look up the getter function
        Symbol *sym = lookupSymbol(getterName);
        if (sym && sym->kind == Symbol::Kind::Function)
        {
            // Return the function's return type (the property type)
            TypeRef funcType = sym->type;
            if (funcType && funcType->kind == TypeKindSem::Function)
            {
                return funcType->returnType();
            }
            return funcType;
        }
    }

    return types::unknown();
}

TypeRef Sema::analyzeOptionalChain(OptionalChainExpr *expr)
{
    TypeRef baseType = analyzeExpr(expr->base.get());

    if (!baseType || baseType->kind != TypeKindSem::Optional)
    {
        error(expr->loc, "Optional chaining requires an optional base value");
        return types::optional(types::unknown());
    }

    TypeRef innerType = baseType->innerType();
    if (!innerType || innerType->kind == TypeKindSem::Unknown)
    {
        return types::optional(types::unknown());
    }

    TypeRef fieldType = types::unknown();

    if (innerType->kind == TypeKindSem::Value || innerType->kind == TypeKindSem::Entity)
    {
        std::string memberKey = innerType->name + "." + expr->field;
        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end())
        {
            fieldType = fieldIt->second;
        }
        else
        {
            error(expr->loc,
                  "Unknown field '" + expr->field + "' on type '" + innerType->name + "'");
        }
    }
    else if (innerType->kind == TypeKindSem::List)
    {
        if (expr->field == "count" || expr->field == "size" || expr->field == "length")
        {
            fieldType = types::integer();
        }
        else
        {
            error(expr->loc, "Unknown field '" + expr->field + "' on List");
        }
    }
    else
    {
        error(expr->loc, "Optional chaining requires a reference type base");
    }

    if (fieldType->kind == TypeKindSem::Optional)
        return fieldType;
    return types::optional(fieldType);
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

bool Sema::analyzeMatchPattern(const MatchArm::Pattern &pattern,
                               TypeRef scrutineeType,
                               MatchCoverage &coverage,
                               std::unordered_map<std::string, TypeRef> &bindings)
{
    auto bind = [&](const std::string &name, TypeRef type)
    {
        if (bindings.find(name) != bindings.end())
        {
            error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                  "Duplicate binding name in pattern: " + name);
            return;
        }
        bindings[name] = type ? type : types::unknown();
    };

    switch (pattern.kind)
    {
        case MatchArm::Pattern::Kind::Wildcard:
            coverage.hasIrrefutable = true;
            return true;

        case MatchArm::Pattern::Kind::Binding:
            if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional &&
                pattern.binding == "None")
            {
                coverage.coversNull = true;
                return true;
            }

            bind(pattern.binding, scrutineeType);
            if (!pattern.guard)
                coverage.hasIrrefutable = true;
            return true;

        case MatchArm::Pattern::Kind::Literal:
        {
            if (pattern.literal)
            {
                TypeRef litType = analyzeExpr(pattern.literal.get());
                if (scrutineeType && !scrutineeType->isAssignableFrom(*litType))
                {
                    error(pattern.literal->loc,
                          "Pattern literal type '" + litType->toString() +
                              "' is not compatible with scrutinee type '" +
                              scrutineeType->toString() + "'");
                }

                if (pattern.literal->kind == ExprKind::IntLiteral)
                {
                    coverage.coveredIntegers.insert(
                        static_cast<IntLiteralExpr *>(pattern.literal.get())->value);
                }
                else if (pattern.literal->kind == ExprKind::BoolLiteral)
                {
                    coverage.coveredBooleans.insert(
                        static_cast<BoolLiteralExpr *>(pattern.literal.get())->value);
                }
                else if (pattern.literal->kind == ExprKind::NullLiteral)
                {
                    coverage.coversNull = true;
                }
            }
            return true;
        }

        case MatchArm::Pattern::Kind::Expression:
            if (pattern.literal)
            {
                TypeRef exprType = analyzeExpr(pattern.literal.get());
                if (exprType->kind != TypeKindSem::Boolean)
                {
                    error(pattern.literal->loc, "Match expression patterns must be Boolean");
                }
            }
            return true;

        case MatchArm::Pattern::Kind::Tuple:
        {
            if (!scrutineeType || scrutineeType->kind != TypeKindSem::Tuple)
            {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Tuple pattern requires tuple scrutinee");
                return false;
            }

            const auto &elements = scrutineeType->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size())
            {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Tuple pattern arity mismatch");
                return false;
            }

            for (size_t i = 0; i < elements.size(); ++i)
            {
                analyzeMatchPattern(pattern.subpatterns[i], elements[i], coverage, bindings);
            }
            return true;
        }

        case MatchArm::Pattern::Kind::Constructor:
        {
            if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
            {
                if (pattern.binding == "Some")
                {
                    coverage.coversSome = true;
                    if (pattern.subpatterns.size() != 1)
                    {
                        error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                              "Some() pattern requires exactly one subpattern");
                        return false;
                    }
                    TypeRef inner = scrutineeType->innerType();
                    analyzeMatchPattern(pattern.subpatterns[0], inner, coverage, bindings);
                    return true;
                }
                if (pattern.binding == "None")
                {
                    coverage.coversNull = true;
                    if (!pattern.subpatterns.empty())
                    {
                        error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                              "None pattern does not take arguments");
                        return false;
                    }
                    return true;
                }

                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Unknown optional constructor pattern: " + pattern.binding);
                return false;
            }

            if (!scrutineeType || (scrutineeType->kind != TypeKindSem::Value &&
                                   scrutineeType->kind != TypeKindSem::Entity))
            {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern requires value or entity scrutinee");
                return false;
            }

            if (pattern.binding != scrutineeType->name)
            {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern '" + pattern.binding +
                          "' does not match scrutinee type '" + scrutineeType->name + "'");
                return false;
            }

            std::vector<TypeRef> fieldTypes;
            if (scrutineeType->kind == TypeKindSem::Value)
            {
                auto it = valueDecls_.find(scrutineeType->name);
                if (it != valueDecls_.end())
                {
                    for (auto &member : it->second->members)
                    {
                        if (member->kind == DeclKind::Field)
                        {
                            auto *field = static_cast<FieldDecl *>(member.get());
                            fieldTypes.push_back(field->type ? resolveTypeNode(field->type.get())
                                                             : types::unknown());
                        }
                    }
                }
            }
            else
            {
                auto it = entityDecls_.find(scrutineeType->name);
                if (it != entityDecls_.end())
                {
                    for (auto &member : it->second->members)
                    {
                        if (member->kind == DeclKind::Field)
                        {
                            auto *field = static_cast<FieldDecl *>(member.get());
                            fieldTypes.push_back(field->type ? resolveTypeNode(field->type.get())
                                                             : types::unknown());
                        }
                    }
                }
            }

            if (fieldTypes.size() != pattern.subpatterns.size())
            {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern field arity mismatch");
                return false;
            }

            for (size_t i = 0; i < fieldTypes.size(); ++i)
            {
                analyzeMatchPattern(pattern.subpatterns[i], fieldTypes[i], coverage, bindings);
            }
            return true;
        }
    }

    return false;
}

TypeRef Sema::analyzeMatchExpr(MatchExpr *expr)
{
    TypeRef scrutineeType = analyzeExpr(expr->scrutinee.get());

    MatchCoverage coverage;
    TypeRef resultType = nullptr;

    for (auto &arm : expr->arms)
    {
        std::unordered_map<std::string, TypeRef> bindings;
        pushScope();

        analyzeMatchPattern(arm.pattern, scrutineeType, coverage, bindings);

        for (const auto &binding : bindings)
        {
            Symbol sym;
            sym.kind = Symbol::Kind::Variable;
            sym.name = binding.first;
            sym.type = binding.second;
            sym.isFinal = true;
            defineSymbol(binding.first, sym);
        }

        if (arm.pattern.guard)
        {
            TypeRef guardType = analyzeExpr(arm.pattern.guard.get());
            if (guardType->kind != TypeKindSem::Boolean)
            {
                error(arm.pattern.guard->loc, "Match guard must be Boolean");
            }
        }

        TypeRef bodyType = analyzeExpr(arm.body.get());
        resultType = commonType(resultType, bodyType);

        popScope();
    }

    if (!coverage.hasIrrefutable)
    {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean)
        {
            if (coverage.coveredBooleans.size() < 2)
            {
                error(expr->loc,
                      "Non-exhaustive patterns: match on Boolean must cover both true "
                      "and false, or use a wildcard (_)");
            }
        }
        else if (scrutineeType && scrutineeType->isIntegral())
        {
            error(expr->loc,
                  "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                  "else case to be exhaustive");
        }
        else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional)
        {
            if (!(coverage.coversNull && coverage.coversSome))
            {
                error(expr->loc,
                      "Non-exhaustive patterns: match on optional type should use a "
                      "wildcard (_) or handle all cases");
            }
        }
    }

    return resultType ? resultType : types::unknown();
}

TypeRef Sema::analyzeNew(NewExpr *expr)
{
    TypeRef type = resolveTypeNode(expr->type.get());

    // Allow new for value/entity types and collection types (List, Set, Map)
    bool allowed = type->kind == TypeKindSem::Value || type->kind == TypeKindSem::Entity ||
                   type->kind == TypeKindSem::List || type->kind == TypeKindSem::Set ||
                   type->kind == TypeKindSem::Map;

    // Also allow new for runtime classes that have a .New constructor
    if (!allowed && type->kind == TypeKindSem::Ptr && !type->name.empty())
    {
        std::string ctorName = type->name + ".New";
        Symbol *sym = lookupSymbol(ctorName);
        if (sym && sym->kind == Symbol::Kind::Function)
        {
            allowed = true;
        }
    }

    if (!allowed)
    {
        error(expr->loc, "'new' can only be used with value, entity, or collection types");
    }

    // Analyze constructor arguments
    for (auto &arg : expr->args)
    {
        analyzeExpr(arg.value.get());
    }

    // For entity types, validate that arguments match init method
    if (type->kind == TypeKindSem::Entity)
    {
        std::string initMethodKey = type->name + ".init";
        auto methodIt = methodTypes_.find(initMethodKey);
        if (methodIt != methodTypes_.end() && methodIt->second)
        {
            // methodTypes_ stores just the declared params (self is added at codegen, not stored here)
            auto params = methodIt->second->paramTypes();
            size_t expectedArgs = params.size();
            size_t providedArgs = expr->args.size();
            if (providedArgs != expectedArgs)
            {
                error(expr->loc, "Entity '" + type->name + "' init() expects " +
                                     std::to_string(expectedArgs) + " argument(s) but got " +
                                     std::to_string(providedArgs));
            }
        }
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
        elementType = commonType(elementType, elemType);
    }

    return types::list(elementType);
}

TypeRef Sema::analyzeMapLiteral(MapLiteralExpr *expr)
{
    TypeRef keyType = types::string();
    TypeRef valueType = types::unknown();

    for (auto &entry : expr->entries)
    {
        TypeRef kType = analyzeExpr(entry.key.get());
        TypeRef vType = analyzeExpr(entry.value.get());

        if (kType->kind != TypeKindSem::String)
        {
            error(entry.key->loc, "Map keys must be String");
        }

        valueType = commonType(valueType, vType);
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

TypeRef Sema::analyzeBlockExpr(BlockExpr *expr)
{
    pushScope();

    // Analyze each statement in the block
    for (auto &stmt : expr->statements)
    {
        analyzeStmt(stmt.get());
    }

    // Analyze the final value expression if present
    TypeRef resultType = types::unit();
    if (expr->value)
    {
        resultType = analyzeExpr(expr->value.get());
    }

    popScope();
    return resultType;
}


} // namespace il::frontends::zia
