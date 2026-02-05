//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr_Call.cpp
/// @brief Call expression analysis and collection method resolution for the
///        Zia semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

#include <string_view>

namespace il::frontends::zia
{

//=============================================================================
// Collection Method Resolution Helpers
//=============================================================================

namespace
{

/// @brief Return type categories for collection methods.
enum class MethodReturnKind
{
    ElementType, ///< Returns the collection's element type
    KeyType,     ///< Returns the map's key type
    ValueType,   ///< Returns the map's value type
    Integer,     ///< Returns Integer
    Boolean,     ///< Returns Boolean
    Void,        ///< Returns Void
    Unknown      ///< Returns Unknown (fallback)
};

/// @brief Descriptor for a collection method's return type.
struct CollectionMethodInfo
{
    std::string_view name;
    MethodReturnKind returnKind;
};

/// @brief List methods and their return types.
const CollectionMethodInfo listMethods[] = {
    // Methods returning element type
    {"get", MethodReturnKind::ElementType},
    {"first", MethodReturnKind::ElementType},
    {"last", MethodReturnKind::ElementType},
    {"pop", MethodReturnKind::ElementType},
    // Methods returning Integer
    {"count", MethodReturnKind::Integer},
    {"size", MethodReturnKind::Integer},
    {"length", MethodReturnKind::Integer},
    {"indexOf", MethodReturnKind::Integer},
    {"lastIndexOf", MethodReturnKind::Integer},
    // Methods returning Boolean
    {"isEmpty", MethodReturnKind::Boolean},
    {"contains", MethodReturnKind::Boolean},
    {"remove", MethodReturnKind::Boolean},
    // Methods returning Void
    {"add", MethodReturnKind::Void},
    {"insert", MethodReturnKind::Void},
    {"set", MethodReturnKind::Void},
    {"clear", MethodReturnKind::Void},
    {"reverse", MethodReturnKind::Void},
    {"sort", MethodReturnKind::Void},
    {"removeAt", MethodReturnKind::Void},
};

/// @brief Map methods and their return types.
const CollectionMethodInfo mapMethods[] = {
    // Methods returning value type
    {"get", MethodReturnKind::ValueType},
    {"getOr", MethodReturnKind::ValueType},
    // Methods returning Void
    {"set", MethodReturnKind::Void},
    {"put", MethodReturnKind::Void},
    {"clear", MethodReturnKind::Void},
    // Methods returning Boolean
    {"setIfMissing", MethodReturnKind::Boolean},
    {"containsKey", MethodReturnKind::Boolean},
    {"hasKey", MethodReturnKind::Boolean},
    {"has", MethodReturnKind::Boolean},
    {"remove", MethodReturnKind::Boolean},
    // Methods returning Integer
    {"size", MethodReturnKind::Integer},
    {"count", MethodReturnKind::Integer},
    {"length", MethodReturnKind::Integer},
    // Methods returning Unknown (iterators)
    {"keys", MethodReturnKind::Unknown},
    {"values", MethodReturnKind::Unknown},
};

/// @brief Set methods and their return types.
const CollectionMethodInfo setMethods[] = {
    // Methods returning Boolean
    {"contains", MethodReturnKind::Boolean},
    {"has", MethodReturnKind::Boolean},
    {"add", MethodReturnKind::Boolean},
    {"remove", MethodReturnKind::Boolean},
    // Methods returning Integer
    {"size", MethodReturnKind::Integer},
    {"count", MethodReturnKind::Integer},
    {"length", MethodReturnKind::Integer},
    // Methods returning Void
    {"clear", MethodReturnKind::Void},
};

/// @brief String methods and their return types.
const CollectionMethodInfo stringMethods[] = {
    {"length", MethodReturnKind::Integer},
    {"count", MethodReturnKind::Integer},
    {"size", MethodReturnKind::Integer},
    {"isEmpty", MethodReturnKind::Boolean},
};

/// @brief Look up a method in a method table.
/// @param methods The method table to search.
/// @param methodName The method name to find.
/// @return The method info if found, nullptr otherwise.
template <std::size_t N>
const CollectionMethodInfo *findMethod(const CollectionMethodInfo (&methods)[N],
                                       std::string_view methodName)
{
    for (const auto &m : methods)
    {
        if (m.name == methodName)
            return &m;
    }
    return nullptr;
}

/// @brief Resolve a return type from a MethodReturnKind.
/// @param kind The return kind.
/// @param baseType The collection type (for element/key/value type resolution).
/// @return The resolved type.
TypeRef resolveMethodReturnType(MethodReturnKind kind, TypeRef baseType)
{
    switch (kind)
    {
        case MethodReturnKind::ElementType:
            return baseType->elementType() ? baseType->elementType() : types::unknown();
        case MethodReturnKind::KeyType:
            return baseType->keyType() ? baseType->keyType() : types::unknown();
        case MethodReturnKind::ValueType:
            return baseType->valueType() ? baseType->valueType() : types::unknown();
        case MethodReturnKind::Integer:
            return types::integer();
        case MethodReturnKind::Boolean:
            return types::boolean();
        case MethodReturnKind::Void:
            return types::voidType();
        case MethodReturnKind::Unknown:
        default:
            return types::unknown();
    }
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

} // anonymous namespace

//=============================================================================
// Call Expression Analysis
//=============================================================================

/// @brief Analyze a function or method call expression.
/// @param expr The call expression node.
/// @return The return type of the called function/method.
/// @details This is a comprehensive method that handles multiple call scenarios:
///          - Generic function calls with explicit type arguments (e.g., identity[Integer](x))
///          - Generic function calls with type inference (e.g., identity(42))
///          - Imported symbol calls from bound namespaces
///          - Qualified function calls (e.g., module.func())
///          - Collection method calls (List, Map, Set, String methods)
///          - Runtime class method calls
///          - Regular function and method calls
TypeRef Sema::analyzeCall(CallExpr *expr)
{
    // Handle generic function calls: identity[Integer](100)
    // Parser produces: CallExpr(callee=IndexExpr(base=IdentExpr, index=IdentExpr/expr), args)
    // We need to detect when the "index" is actually a type argument
    if (expr->callee->kind == ExprKind::Index)
    {
        auto *indexExpr = static_cast<IndexExpr *>(expr->callee.get());
        if (indexExpr->base->kind == ExprKind::Ident)
        {
            auto *identExpr = static_cast<IdentExpr *>(indexExpr->base.get());
            if (isGenericFunction(identExpr->name))
            {
                // This is a generic function call!
                // The "index" should be a type name
                std::vector<TypeRef> typeArgs;

                // Try to interpret the index expression as a type
                if (indexExpr->index->kind == ExprKind::Ident)
                {
                    auto *typeIdent = static_cast<IdentExpr *>(indexExpr->index.get());
                    // Create a NamedType node and resolve it
                    auto typeNode = std::make_unique<NamedType>(typeIdent->loc, typeIdent->name);
                    TypeRef typeArg = resolveTypeNode(typeNode.get());
                    if (typeArg && typeArg->kind != TypeKindSem::Unknown)
                    {
                        typeArgs.push_back(typeArg);
                    }
                    else
                    {
                        error(typeIdent->loc, "Unknown type: " + typeIdent->name);
                        return types::unknown();
                    }
                }
                else
                {
                    error(indexExpr->index->loc,
                          "Expected type argument for generic function call");
                    return types::unknown();
                }

                // Instantiate the generic function with the type arguments
                TypeRef funcType = instantiateGenericFunction(identExpr->name, typeArgs, expr->loc);

                // Store the mangled name for the lowerer
                std::string mangledName = mangleGenericName(identExpr->name, typeArgs);
                genericFunctionCallees_[expr] = mangledName;

                // Store the instantiated function type so the lowerer can access it
                exprTypes_[expr->callee.get()] = funcType;

                // Analyze arguments
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }

                // Return the function's return type
                if (funcType && funcType->kind == TypeKindSem::Function)
                {
                    return funcType->returnType();
                }
                return types::unknown();
            }
        }
    }

    // Type inference for generic function calls without explicit type arguments
    // e.g., identity(42) instead of identity[Integer](42)
    // This must come BEFORE the dotted name lookup to catch simple IdentExpr callees
    if (expr->callee->kind == ExprKind::Ident)
    {
        auto *identExpr = static_cast<IdentExpr *>(expr->callee.get());
        if (isGenericFunction(identExpr->name))
        {
            FunctionDecl *genericDecl = getGenericFunction(identExpr->name);
            if (genericDecl && !genericDecl->genericParams.empty() && !expr->args.empty())
            {
                // Analyze all arguments first to get their types
                std::vector<TypeRef> argTypes;
                for (auto &arg : expr->args)
                {
                    TypeRef argType = analyzeExpr(arg.value.get());
                    argTypes.push_back(argType);
                }

                // Build set of type parameter names for quick lookup
                std::set<std::string> typeParamNames(genericDecl->genericParams.begin(),
                                                     genericDecl->genericParams.end());

                // Infer type parameters from argument types
                std::map<std::string, TypeRef> inferredTypes;
                for (size_t i = 0; i < genericDecl->params.size() && i < argTypes.size(); ++i)
                {
                    // Check if the parameter type is a type parameter (e.g., T)
                    TypeNode *paramTypeNode = genericDecl->params[i].type.get();
                    if (paramTypeNode && paramTypeNode->kind == TypeKind::Named)
                    {
                        auto *namedType = static_cast<NamedType *>(paramTypeNode);
                        // Check if this name is a type parameter
                        if (typeParamNames.count(namedType->name) > 0)
                        {
                            // This parameter has type T, infer T from the argument
                            const std::string &typeParamName = namedType->name;
                            TypeRef argType = argTypes[i];
                            if (argType && argType->kind != TypeKindSem::Unknown)
                            {
                                // Check for consistency if already inferred
                                auto it = inferredTypes.find(typeParamName);
                                if (it != inferredTypes.end())
                                {
                                    if (it->second != argType)
                                    {
                                        error(expr->args[i].value->loc,
                                              "Type mismatch in generic function call: "
                                              "cannot infer consistent type for " +
                                                  typeParamName);
                                        return types::unknown();
                                    }
                                }
                                else
                                {
                                    inferredTypes[typeParamName] = argType;
                                }
                            }
                        }
                    }
                }

                // Check that all type parameters were inferred
                std::vector<TypeRef> typeArgs;
                for (const auto &paramName : genericDecl->genericParams)
                {
                    auto it = inferredTypes.find(paramName);
                    if (it != inferredTypes.end())
                    {
                        typeArgs.push_back(it->second);
                    }
                    else
                    {
                        error(expr->loc,
                              "Cannot infer type argument for '" + paramName +
                                  "' in generic function call");
                        return types::unknown();
                    }
                }

                // Instantiate the generic function with inferred type arguments
                TypeRef funcType = instantiateGenericFunction(identExpr->name, typeArgs, expr->loc);

                // Store the mangled name for the lowerer
                std::string mangledName = mangleGenericName(identExpr->name, typeArgs);
                genericFunctionCallees_[expr] = mangledName;

                // Store the instantiated function type
                exprTypes_[expr->callee.get()] = funcType;

                // Return the function's return type
                if (funcType && funcType->kind == TypeKindSem::Function)
                {
                    return funcType->returnType();
                }
                return types::unknown();
            }
        }
    }

    // Check if callee is an imported symbol from a bound namespace
    // This handles unqualified calls like Say() when Viper.Terminal is bound
    if (expr->callee->kind == ExprKind::Ident)
    {
        auto *identExpr = static_cast<IdentExpr *>(expr->callee.get());
        auto importIt = importedSymbols_.find(identExpr->name);
        if (importIt != importedSymbols_.end())
        {
            // Resolve to the full qualified name
            const std::string &fullName = importIt->second;
            Symbol *sym = lookupSymbol(fullName);
            if (sym && sym->kind == Symbol::Kind::Function && sym->isExtern)
            {
                // Store the resolved callee for the lowerer
                runtimeCallees_[expr] = fullName;
                exprTypes_[expr->callee.get()] = sym->type;

                // Analyze arguments
                for (auto &arg : expr->args)
                {
                    analyzeExpr(arg.value.get());
                }

                // Return the function's return type
                if (sym->type && sym->type->kind == TypeKindSem::Function)
                {
                    return sym->type->returnType();
                }
                return sym->type;
            }
        }
    }

    // First, try to resolve dotted function names like Viper.Terminal.Say or MyLib.helper
    // This unified lookup works for both runtime functions and user-defined namespaced functions
    std::string dottedName;
    if (extractDottedName(expr->callee.get(), dottedName))
    {
        // Check if the first part is a module alias or imported symbol that needs expansion
        // e.g., "T.Say" where T is an alias for "Viper.Terminal" becomes "Viper.Terminal.Say"
        // or "Canvas.New" where Canvas is imported from Viper.Graphics becomes "Viper.Graphics.Canvas.New"
        auto dotPos = dottedName.find('.');
        if (dotPos != std::string::npos)
        {
            std::string firstPart = dottedName.substr(0, dotPos);
            std::string rest = dottedName.substr(dotPos + 1);

            // Check if firstPart is a module alias (bound namespace with alias)
            for (const auto &[ns, alias] : boundNamespaces_)
            {
                if (!alias.empty() && alias == firstPart)
                {
                    // Expand the alias: T.Say -> Viper.Terminal.Say
                    dottedName = ns + "." + rest;
                    break;
                }
            }

            // Check if firstPart is an imported symbol (e.g., Canvas from Viper.Graphics)
            auto importIt = importedSymbols_.find(firstPart);
            if (importIt != importedSymbols_.end())
            {
                // Expand: Canvas.New -> Viper.Graphics.Canvas.New
                dottedName = importIt->second + "." + rest;
            }
        }

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

        // Helper to analyze all arguments
        auto analyzeArgs = [&]()
        {
            for (auto &arg : expr->args)
            {
                analyzeExpr(arg.value.get());
            }
        };

        // Handle List methods using lookup table
        if (baseType && baseType->kind == TypeKindSem::List)
        {
            if (auto *method = findMethod(listMethods, fieldExpr->field))
            {
                // Special handling for remove/contains type checking
                if (fieldExpr->field == "remove" || fieldExpr->field == "contains")
                {
                    TypeRef elemType = baseType->elementType();
                    for (auto &arg : expr->args)
                    {
                        TypeRef argType = analyzeExpr(arg.value.get());
                        if (elemType && argType && !expr->args.empty() &&
                            argType->kind == TypeKindSem::Integer &&
                            elemType->kind != TypeKindSem::Integer)
                        {
                            error(expr->args[0].value->loc,
                                  "Type mismatch: " + fieldExpr->field +
                                      "() expects element type, got Integer. "
                                      "Did you mean removeAt() to remove by index?");
                        }
                    }
                }
                else
                {
                    analyzeArgs();
                }
                return resolveMethodReturnType(method->returnKind, baseType);
            }
        }

        // Handle Map methods using lookup table
        if (baseType && baseType->kind == TypeKindSem::Map)
        {
            if (auto *method = findMethod(mapMethods, fieldExpr->field))
            {
                analyzeArgs();
                // Validate string keys for methods that require them
                if (method->returnKind == MethodReturnKind::ValueType ||
                    method->returnKind == MethodReturnKind::Boolean ||
                    fieldExpr->field == "set" || fieldExpr->field == "put")
                {
                    if (!expr->args.empty())
                    {
                        TypeRef keyType = exprTypes_[expr->args[0].value.get()];
                        if (keyType && keyType->kind != TypeKindSem::String &&
                            keyType->kind != TypeKindSem::Unknown)
                        {
                            error(expr->args[0].value->loc, "Map keys must be String");
                        }
                    }
                }
                return resolveMethodReturnType(method->returnKind, baseType);
            }
        }

        // Handle Set methods using lookup table
        if (baseType && baseType->kind == TypeKindSem::Set)
        {
            if (auto *method = findMethod(setMethods, fieldExpr->field))
            {
                analyzeArgs();
                return resolveMethodReturnType(method->returnKind, baseType);
            }
        }

        // Fallback: Map semantic collection types to runtime class methods.
        // Handles runtime-specific methods (get_Len, Put, First, etc.) that aren't
        // in the built-in Zia-friendly method tables above.
        if (baseType && (baseType->kind == TypeKindSem::Set ||
                         baseType->kind == TypeKindSem::List ||
                         baseType->kind == TypeKindSem::Map))
        {
            std::string className;
            if (baseType->kind == TypeKindSem::Set)
                className = "Viper.Collections.Set";
            else if (baseType->kind == TypeKindSem::List)
                className = "Viper.Collections.List";
            else
                className = "Viper.Collections.Map";

            std::string fullMethodName = className + "." + fieldExpr->field;
            Symbol *sym = lookupSymbol(fullMethodName);
            if (sym && sym->kind == Symbol::Kind::Function)
            {
                analyzeArgs();
                if (sym->isExtern)
                {
                    runtimeCallees_[expr] = fullMethodName;
                }
                if (sym->type && sym->type->kind == TypeKindSem::Function)
                    return sym->type->returnType();
                return sym->type;
            }
        }

        // Handle String methods using lookup table
        if (baseType && baseType->kind == TypeKindSem::String)
        {
            if (auto *method = findMethod(stringMethods, fieldExpr->field))
            {
                analyzeArgs();
                return resolveMethodReturnType(method->returnKind, baseType);
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

} // namespace il::frontends::zia
