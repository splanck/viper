//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Expr_Advanced.cpp
/// @brief Advanced expression analysis (index, field, optional chain, type
///        operators, pattern matching, collections, etc.) for the Zia semantic
///        analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <unordered_set>

namespace il::frontends::zia {

namespace {

/// @brief Try to extract a dotted name from a field access chain.
/// @param expr The expression to extract from.
/// @param out The output string to append to.
/// @return True if successful, false otherwise.
static bool extractDottedName(Expr *expr, std::string &out) {
    if (!expr)
        return false;
    if (expr->kind == ExprKind::Ident) {
        auto *ident = static_cast<IdentExpr *>(expr);
        out = ident->name;
        return true;
    }
    if (expr->kind == ExprKind::Field) {
        auto *fieldExpr = static_cast<FieldExpr *>(expr);
        if (!extractDottedName(fieldExpr->base.get(), out))
            return false;
        out += ".";
        out += fieldExpr->field;
        return true;
    }
    return false;
}

/// @brief True if @p name is one of the size/length property spellings
///        (Length/Len/Count/size, any case) treated as a count accessor.
static bool isCountLikeProperty(const std::string &name) {
    return name == "Length" || name == "length" || name == "Len" || name == "Count" ||
           name == "count" || name == "size";
}

} // anonymous namespace

//=============================================================================
// Index and Field Access
//=============================================================================

/// @brief Analyze an index expression (e.g., list[i], map["key"]).
/// @param expr The index expression node.
/// @return The element type for lists/strings, struct type for maps.
/// @details Validates index type (integral for lists, string for maps).
TypeRef Sema::analyzeIndex(IndexExpr *expr) {
    TypeRef baseType = analyzeExpr(expr->base.get());
    TypeRef indexType = analyzeExpr(expr->index.get());

    if (baseType->kind == TypeKindSem::List || baseType->kind == TypeKindSem::String) {
        if (!indexType->isIntegral()) {
            error(expr->index->loc, "Index must be an integer");
        }
        if (baseType->kind == TypeKindSem::String)
            return types::string();
        return baseType->elementType() ? baseType->elementType() : types::unknown();
    }

    if (baseType->kind == TypeKindSem::Map) {
        if (indexType->kind != TypeKindSem::String) {
            error(expr->index->loc, "Map keys must be String");
        }
        return baseType->valueType() ? baseType->valueType() : types::unknown();
    }

    // Fixed-size array: T[N] — element access returns the element type
    if (baseType->kind == TypeKindSem::FixedArray) {
        if (!indexType->isIntegral()) {
            error(expr->index->loc, "Index must be an integer");
        } else if (auto *literal = dynamic_cast<IntLiteralExpr *>(expr->index.get())) {
            if (literal->value < 0 ||
                static_cast<size_t>(literal->value) >= baseType->elementCount) {
                error(expr->index->loc,
                      "fixed array index " + std::to_string(literal->value) +
                          " is out of bounds for length " + std::to_string(baseType->elementCount));
            }
        }
        return baseType->elementType() ? baseType->elementType() : types::unknown();
    }

    error(expr->loc, "Expression is not indexable");
    return types::unknown();
}

/// @brief Analyze a field access expression (e.g., obj.field, Type.method).
/// @param expr The field expression node.
/// @return The type of the accessed field, method, or property.
/// @details Handles multiple cases:
///          - Runtime class property access (e.g., Viper.Math.Pi)
///          - Module-qualified access (e.g., colors.initColors)
///          - Entity/Value field and method access with visibility checking
///          - Built-in collection properties (e.g., list.count)
TypeRef Sema::analyzeField(FieldExpr *expr) {
    // BUG-012 fix: Handle runtime class namespace property access (e.g., Viper.Math.Pi)
    // For property access like Viper.Math.Pi, we need to resolve it as a getter call
    // before trying to analyze the base, because "Viper" is not a symbol.
    std::string dottedBase;
    if (extractDottedName(expr->base.get(), dottedBase)) {
        // Check if the dotted base is a runtime class (registered in typeRegistry_)
        auto typeIt = typeRegistry_.find(dottedBase);
        if (typeIt != typeRegistry_.end()) {
            if (TypeRef staticFieldType = resolveStaticField(expr, dottedBase))
                return staticFieldType;

            // Try to find a getter function: {ClassName}.get_{PropertyName}
            std::string getterName = dottedBase + ".get_" + expr->field;
            Symbol *sym = lookupAccessibleSymbol(getterName, expr->loc, true);
            if (sym && sym->kind == Symbol::Kind::Function) {
                // Store the resolved getter for the lowerer
                resolvedFieldGetters_[expr] = getterName;
                // Return the function's return type (the property type)
                TypeRef funcType = sym->type;
                if (funcType && funcType->kind == TypeKindSem::Function) {
                    return normalizeRuntimeSurfaceType(funcType->returnType());
                }
                return normalizeRuntimeSurfaceType(funcType);
            }

            // Try direct function lookup (e.g., Viper.Result.Ok, Viper.Text.Uuid.New)
            std::string funcName = dottedBase + "." + expr->field;
            sym = lookupAccessibleSymbol(funcName, expr->loc, true);
            if (sym && sym->kind == Symbol::Kind::Function) {
                if (sym->isExtern && sym->type && sym->type->kind == TypeKindSem::Function &&
                    sym->type->paramTypes().empty()) {
                    resolvedFieldGetters_[expr] = funcName;
                    return normalizeRuntimeSurfaceType(sym->type->returnType());
                }
                return sym->type;
            }

            // Enum variant access (e.g., Color.Red)
            if (typeIt->second && typeIt->second->kind == TypeKindSem::Enum) {
                std::string key = dottedBase + "." + expr->field;
                auto fieldIt = fieldTypes_.find(key);
                if (fieldIt != fieldTypes_.end()) {
                    return fieldIt->second;
                }
                error(expr->loc, "Enum '" + dottedBase + "' has no variant '" + expr->field + "'");
                return types::unknown();
            }

            // Return a module type so downstream code can resolve further
            return types::module(dottedBase);
        }

        // Check if the dotted base + field together form a known type
        std::string fullDotted = dottedBase + "." + expr->field;
        typeIt = typeRegistry_.find(fullDotted);
        if (typeIt != typeRegistry_.end()) {
            return types::module(fullDotted);
        }
    }

    TypeRef baseType = analyzeExpr(expr->base.get());

    if (TypeRef narrowed = lookupNarrowedType(narrowingKeyForExpr(expr))) {
        return narrowed;
    }

    // Optional member access must use flow narrowing, force unwrap, or `?.`.
    if (baseType && baseType->kind == TypeKindSem::Optional && baseType->innerType()) {
        error(expr->loc,
              "Cannot access member '" + expr->field + "' on Optional type '" +
                  baseType->toDisplayString() +
                  "' without null check; use optional chaining or force unwrap");
        return types::unknown();
    }

    // Handle module-qualified access (e.g., colors.initColors or Canvas.New)
    if (baseType && baseType->kind == TypeKindSem::Module) {
        return resolveModuleFieldAccess(expr, baseType);
    }

    // Check if this is an enum variant access (e.g., Color.Red)
    if (baseType && baseType->kind == TypeKindSem::Enum) {
        std::string key = baseType->name + "." + expr->field;
        auto fieldIt = fieldTypes_.find(key);
        if (fieldIt != fieldTypes_.end()) {
            return fieldIt->second;
        }
        error(expr->loc, "Enum '" + baseType->name + "' has no variant '" + expr->field + "'");
        return types::unknown();
    }

    // Field/method/property access on a class or struct.
    if (baseType &&
        (baseType->kind == TypeKindSem::Struct || baseType->kind == TypeKindSem::Class)) {
        return resolveClassStructFieldAccess(expr, baseType);
    }

    // Handle built-in properties like .Length on lists
    if (baseType && baseType->kind == TypeKindSem::List) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
        error(expr->loc, "Unknown field '" + expr->field + "' on List");
        return types::unknown();
    }

    // Handle built-in properties on maps (.Length, .Len, .Count, etc.)
    if (baseType && baseType->kind == TypeKindSem::Map) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
        error(expr->loc, "Unknown field '" + expr->field + "' on Map");
        return types::unknown();
    }

    // Handle built-in properties on sets (.Length, .Len, .Count, etc.)
    if (baseType && baseType->kind == TypeKindSem::Set) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
        error(expr->loc, "Unknown field '" + expr->field + "' on Set");
        return types::unknown();
    }

    // Handle built-in properties on strings (Bug #3 fix)
    if (baseType && baseType->kind == TypeKindSem::String) {
        if (expr->field == "Length" || expr->field == "length") {
            return types::integer();
        }
        error(expr->loc, "Unknown field '" + expr->field + "' on String");
        return types::unknown();
    }

    // Structured catch bindings expose stable error metadata.
    if (baseType && baseType->kind == TypeKindSem::Error) {
        if (expr->field == "kind" || expr->field == "type" || expr->field == "message" ||
            expr->field == "location") {
            return types::string();
        }
        if (expr->field == "code" || expr->field == "line") {
            return types::integer();
        }
        error(expr->loc, "Unknown field '" + expr->field + "' on Error");
        return types::unknown();
    }

    // Reject field access on primitive types that have no members
    if (baseType &&
        (baseType->kind == TypeKindSem::Integer || baseType->kind == TypeKindSem::Number ||
         baseType->kind == TypeKindSem::Boolean || baseType->kind == TypeKindSem::Byte)) {
        error(expr->loc,
              "Type '" + baseType->toDisplayString() + "' has no member '" + expr->field + "'");
        return types::unknown();
    }

    // Handle runtime class property access (e.g., app.Root, editor.LineCount)
    // Runtime classes are Ptr types with a name like "Viper.GUI.App"
    if (baseType && baseType->kind == TypeKindSem::Ptr && !baseType->name.empty() &&
        baseType->name.find("Viper.") == 0) {
        const auto &registry = il::runtime::RuntimeRegistry::instance();
        std::string getterName = baseType->name + ".get_" + expr->field;
        if (auto prop = registry.findProperty(baseType->name, expr->field); prop) {
            if (!prop->getter || !*prop->getter) {
                error(expr->loc,
                      "Property '" + expr->field + "' of type '" + baseType->name +
                          "' is write-only");
                return types::unknown();
            }
            getterName = prop->getter;
        }

        Symbol *sym = lookupAccessibleSymbol(getterName, expr->loc, true);
        if (sym && sym->kind == Symbol::Kind::Function) {
            resolvedFieldGetters_[expr] = getterName;
            // Return the function's return type (the property type)
            TypeRef funcType = sym->type;
            if (funcType && funcType->kind == TypeKindSem::Function) {
                return normalizeRuntimeSurfaceType(funcType->returnType());
            }
            return normalizeRuntimeSurfaceType(funcType);
        }
    }

    return types::unknown();
}

TypeRef Sema::resolveStaticField(FieldExpr *expr, const std::string &ownerName) {
    std::string fieldKey = ownerName + "." + expr->field;
    if (!staticFields_.contains(fieldKey))
        return nullptr;

    auto fieldIt = fieldTypes_.find(fieldKey);
    if (fieldIt == fieldTypes_.end())
        return nullptr;

    bool isInsideType = currentSelfType_ && currentSelfType_->name == ownerName;
    auto visIt = memberVisibility_.find(fieldKey);
    if (visIt != memberVisibility_.end() && visIt->second == Visibility::Private && !isInsideType) {
        error(expr->loc,
              "Cannot access private member '" + expr->field + "' of type '" + ownerName + "'");
        return types::unknown();
    }

    exprTypes_[expr->base.get()] = types::module(ownerName);
    return fieldIt->second;
}

TypeRef Sema::resolveModuleFieldAccess(FieldExpr *expr, TypeRef baseType) {
    if (TypeRef staticFieldType = resolveStaticField(expr, baseType->name))
        return staticFieldType;

    if (auto moduleIt = moduleExports_.find(baseType->name); moduleIt != moduleExports_.end()) {
        auto exportIt = moduleIt->second.find(expr->field);
        if (exportIt != moduleIt->second.end()) {
            const Symbol &sym = exportIt->second;
            if (sym.kind == Symbol::Kind::Function && hasOverloadedFunctionName(sym.name)) {
                error(expr->loc,
                      "Member '" + expr->field +
                          "' is overloaded and must be called with arguments to resolve it");
                return types::unknown();
            }
            resolvedFieldSymbolNames_[expr] = sym.name;
            return sym.type;
        }
    }

    // Build the full qualified name (e.g., Viper.Graphics.Canvas.New).
    std::string fullName = baseType->name + "." + expr->field;

    // First try to look up the qualified name directly.
    Symbol *sym = lookupAccessibleSymbol(fullName, expr->loc, true);
    if (sym) {
        resolvedFieldSymbolNames_[expr] = sym->name;
        if (sym->kind == Symbol::Kind::Function && sym->isExtern && sym->type &&
            sym->type->kind == TypeKindSem::Function && sym->type->paramTypes().empty()) {
            resolvedFieldGetters_[expr] = fullName;
            return normalizeRuntimeSurfaceType(sym->type->returnType());
        }
        return sym->type;
    }

    // For runtime classes (Viper.*), the symbol might not be in the symbol table
    // but could be a valid runtime method. Check importedSymbols_ for the method.
    auto importIt = importedSymbols_.find(fullName);
    if (importIt != importedSymbols_.end()) {
        return types::module(importIt->second);
    }

    // For local modules, also try unqualified name (for backwards compatibility).
    sym = lookupAccessibleSymbol(expr->field, expr->loc, true);
    if (sym) {
        return sym->type;
    }

    // Check if fullName is a valid runtime class or sub-namespace
    // (e.g., "Viper.Collections.List" when accessed via alias "Collections.List").
    if (isValidRuntimeNamespace(fullName)) {
        return types::module(fullName);
    }

    // Also check typeRegistry_ for the qualified name directly (handles runtime
    // types that are registered but not in importedSymbols_).
    auto typeIt = typeRegistry_.find(fullName);
    if (typeIt != typeRegistry_.end()) {
        return typeIt->second;
    }

    // Try the getter convention (get_PropertyName) for static properties
    // on runtime classes. Properties like Color.RED are registered as
    // "Viper.Graphics.Color.get_RED" in the symbol table.
    {
        std::string getterName = baseType->name + ".get_" + expr->field;
        Symbol *getter = lookupAccessibleSymbol(getterName, expr->loc, true);
        if (getter && getter->kind == Symbol::Kind::Function) {
            resolvedFieldGetters_[expr] = getterName;
            TypeRef funcType = getter->type;
            if (funcType && funcType->kind == TypeKindSem::Function)
                return normalizeRuntimeSurfaceType(funcType->returnType());
            return normalizeRuntimeSurfaceType(funcType);
        }
    }

    // If not found in global scope, report error.
    error(expr->loc,
          "Module '" + baseType->name + "' has no exported symbol '" + expr->field + "'");
    return types::unknown();
}

TypeRef Sema::resolveClassStructFieldAccess(FieldExpr *expr, TypeRef baseType) {
    std::string memberKey = baseType->name + "." + expr->field;
    bool isInsideType = currentSelfType_ && currentSelfType_->name == baseType->name;

    auto visIt = memberVisibility_.find(memberKey);
    if (visIt != memberVisibility_.end()) {
        if (visIt->second == Visibility::Private && !isInsideType) {
            error(expr->loc,
                  "Cannot access private member '" + expr->field + "' of type '" + baseType->name +
                      "'");
        }
    }

    // Method?
    auto overloads = collectMethodOverloads(baseType->name, expr->field, true);
    if (!overloads.empty()) {
        if (overloads.size() > 1) {
            error(expr->loc,
                  "Member '" + expr->field +
                      "' is overloaded and must be called with arguments to resolve it");
            return types::unknown();
        }

        MethodDecl *method = overloads.front();
        if (method->visibility == Visibility::Private && !isInsideType) {
            error(expr->loc,
                  "Cannot access private member '" + expr->field + "' of type '" + baseType->name +
                      "'");
            return types::unknown();
        }
        TypeRef methodType = getMethodType(baseType->name, method);
        if (methodType)
            return methodType;
    }

    // Field?
    auto fieldIt = fieldTypes_.find(memberKey);
    if (fieldIt != fieldTypes_.end()) {
        return fieldIt->second;
    }

    // Property?
    std::string declaringOwner;
    if (const PropertyDecl *prop =
            propertyDeclForLowering(baseType->name, expr->field, &declaringOwner)) {
        if (prop->visibility == Visibility::Private && !isInsideType) {
            error(expr->loc,
                  "Cannot access private member '" + expr->field + "' of type '" + declaringOwner +
                      "'");
            return types::unknown();
        }
        if (!prop->getterBody) {
            error(expr->loc,
                  "Property '" + expr->field + "' of type '" + declaringOwner + "' is write-only");
            return types::unknown();
        }

        resolvedFieldGetters_[expr] = declaringOwner + ".get_" + prop->name;
        return prop->type ? resolveTypeNode(prop->type.get()) : types::unknown();
    }

    error(expr->loc, "Type '" + baseType->name + "' has no member '" + expr->field + "'");
    return types::unknown();
}

//=============================================================================
// Optional and Type Operators
//=============================================================================

TypeRef Sema::analyzeForceUnwrap(ForceUnwrapExpr *expr) {
    TypeRef operandType = analyzeExpr(expr->operand.get());
    operandType = declaredOptionalSurfaceType(expr->operand.get(), operandType);
    exprTypes_[expr->operand.get()] = operandType;

    if (!operandType || operandType->kind != TypeKindSem::Optional) {
        // Reference types (Entity, Ptr, String) are nullable at the IL level, so
        // force-unwrapping them is valid even when narrowing has already removed the
        // Optional wrapper. This supports the common guard-clause pattern:
        //   if x == null { return; }
        //   var y = x!;  // redundant but valid
        // For non-nullable types (Integer, Number, Boolean, Byte), '!' is an error.
        if (operandType &&
            (operandType->kind == TypeKindSem::Class || operandType->kind == TypeKindSem::Struct ||
             operandType->kind == TypeKindSem::Ptr || operandType->kind == TypeKindSem::String ||
             operandType->kind == TypeKindSem::Interface ||
             operandType->kind == TypeKindSem::List || operandType->kind == TypeKindSem::Map ||
             operandType->kind == TypeKindSem::Set || operandType->kind == TypeKindSem::Any ||
             operandType->kind == TypeKindSem::Unknown)) {
            return operandType;
        }

        error(expr->loc,
              "Force-unwrap '!' requires an optional type, got " +
                  (operandType ? operandType->toDisplayString() : "unknown"));
        return operandType ? operandType : types::unknown();
    }

    TypeRef inner = operandType->innerType();
    return inner ? inner : types::unknown();
}

TypeRef Sema::analyzeOptionalChain(OptionalChainExpr *expr) {
    TypeRef baseType = analyzeExpr(expr->base.get());
    baseType = declaredOptionalSurfaceType(expr->base.get(), baseType);
    exprTypes_[expr->base.get()] = baseType;

    if (!baseType || baseType->kind != TypeKindSem::Optional) {
        error(expr->loc, "Optional chaining requires an optional base value");
        return types::optional(types::unknown());
    }

    TypeRef innerType = baseType->innerType();
    if (!innerType || innerType->kind == TypeKindSem::Unknown) {
        return types::optional(types::unknown());
    }

    TypeRef fieldType = types::unknown();

    if (innerType->kind == TypeKindSem::Struct || innerType->kind == TypeKindSem::Class) {
        std::string memberKey = innerType->name + "." + expr->field;
        bool isInsideType = currentSelfType_ && currentSelfType_->name == innerType->name;
        auto visIt = memberVisibility_.find(memberKey);
        if (visIt != memberVisibility_.end() && visIt->second == Visibility::Private &&
            !isInsideType) {
            error(expr->loc,
                  "Cannot access private member '" + expr->field + "' of type '" + innerType->name +
                      "'");
            return types::optional(types::unknown());
        }

        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end()) {
            fieldType = fieldIt->second;
        } else if (const PropertyDecl *prop =
                       propertyDeclForLowering(innerType->name, expr->field, nullptr)) {
            if (prop->visibility == Visibility::Private && !isInsideType) {
                error(expr->loc,
                      "Cannot access private member '" + expr->field + "' of type '" +
                          innerType->name + "'");
                return types::optional(types::unknown());
            }
            if (!prop->getterBody) {
                error(expr->loc,
                      "Property '" + expr->field + "' of type '" + innerType->name +
                          "' is write-only");
                return types::optional(types::unknown());
            }
            fieldType = prop->type ? resolveTypeNode(prop->type.get()) : types::unknown();
        } else {
            error(expr->loc,
                  "Unknown field '" + expr->field + "' on type '" + innerType->name + "'");
        }
    } else if (innerType->kind == TypeKindSem::List) {
        if (isCountLikeProperty(expr->field)) {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on List");
        }
    } else if (innerType->kind == TypeKindSem::Map) {
        if (isCountLikeProperty(expr->field)) {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on Map");
        }
    } else if (innerType->kind == TypeKindSem::Set) {
        if (isCountLikeProperty(expr->field)) {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on Set");
        }
    } else if (innerType->kind == TypeKindSem::String) {
        if (expr->field == "Length" || expr->field == "length") {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on String");
        }
    } else if (innerType->kind == TypeKindSem::Ptr && !innerType->name.empty() &&
               innerType->name.find("Viper.") == 0) {
        const auto &registry = il::runtime::RuntimeRegistry::instance();
        std::string getterName = innerType->name + ".get_" + expr->field;
        if (auto prop = registry.findProperty(innerType->name, expr->field); prop) {
            if (!prop->getter || !*prop->getter) {
                error(expr->loc,
                      "Property '" + expr->field + "' of type '" + innerType->name +
                          "' is write-only");
                return types::optional(types::unknown());
            }
            getterName = prop->getter;
        }

        Symbol *sym = lookupAccessibleSymbol(getterName, expr->loc, true);
        if (sym && sym->kind == Symbol::Kind::Function) {
            TypeRef funcType = sym->type;
            if (funcType && funcType->kind == TypeKindSem::Function) {
                fieldType = normalizeRuntimeSurfaceType(funcType->returnType());
            } else {
                fieldType = normalizeRuntimeSurfaceType(funcType);
            }
        } else {
            error(expr->loc,
                  "Unknown property '" + expr->field + "' on type '" + innerType->name + "'");
        }
    } else {
        error(expr->loc, "Optional chaining requires a reference type base");
    }

    if (fieldType->kind == TypeKindSem::Optional)
        return fieldType;
    return types::optional(fieldType);
}

/// @brief Analyze a null-coalescing expression (left ?? right).
/// @param expr The coalesce expression node.
/// @return The unwrapped type (non-optional) of the left operand.
/// @details Returns right value if left is null/None.
TypeRef Sema::analyzeCoalesce(CoalesceExpr *expr) {
    TypeRef leftType = analyzeExpr(expr->left.get());
    leftType = declaredOptionalSurfaceType(expr->left.get(), leftType);
    exprTypes_[expr->left.get()] = leftType;

    TypeRef rightType = analyzeExpr(expr->right.get());

    if (!leftType || leftType->kind == TypeKindSem::Unknown)
        return rightType ? rightType : types::unknown();

    if (leftType->kind != TypeKindSem::Optional) {
        error(expr->loc,
              "Null-coalescing operator requires an optional left operand, got " +
                  leftType->toDisplayString());
        return leftType;
    }

    TypeRef innerType = leftType->innerType();
    if (!innerType)
        return rightType ? rightType : types::unknown();

    if (rightType && rightType->kind != TypeKindSem::Unknown &&
        !innerType->isAssignableFrom(*rightType)) {
        errorTypeMismatch(expr->right->loc, innerType, rightType);
    }

    return innerType;
}

TypeRef Sema::analyzeTry(TryExpr *expr) {
    TypeRef operandType = analyzeExpr(expr->operand.get());
    operandType = declaredOptionalSurfaceType(expr->operand.get(), operandType);
    exprTypes_[expr->operand.get()] = operandType;

    if (!operandType || operandType->kind == TypeKindSem::Unknown)
        return types::unknown();

    if (operandType->kind != TypeKindSem::Optional && operandType->kind != TypeKindSem::Result) {
        error(expr->loc,
              "Try expression '?' requires an optional or Result operand, got " +
                  operandType->toDisplayString());
        return operandType;
    }

    TypeRef innerType =
        operandType->kind == TypeKindSem::Optional
            ? (operandType->innerType() ? operandType->innerType() : types::unknown())
            : (!operandType->typeArgs.empty() ? operandType->typeArgs[0] : types::unknown());

    if (!expectedReturnType_) {
        error(expr->loc, "Try expression '?' can only be used inside a function");
        return innerType;
    }

    if (operandType->kind == TypeKindSem::Result) {
        if (expectedReturnType_->kind != TypeKindSem::Result) {
            error(
                expr->loc,
                "Try expression '?' can only propagate a Result from a function returning Result");
        }
        return innerType;
    }

    if (expectedReturnType_->kind != TypeKindSem::Optional) {
        error(expr->loc,
              "Try expression '?' can only propagate an optional from a function returning an "
              "optional type");
        return innerType;
    }

    TypeRef returnInner = expectedReturnType_->innerType();
    if (returnInner && innerType && innerType->kind != TypeKindSem::Unknown &&
        !returnInner->isAssignableFrom(*innerType)) {
        error(expr->loc,
              "Try expression '?' unwraps " + innerType->toDisplayString() +
                  " but the enclosing function returns " + expectedReturnType_->toDisplayString());
    }

    return innerType;
}

/// @brief Analyze a type check expression (value is Type).
/// @param expr The is expression node.
/// @return Boolean type (result of type check).
TypeRef Sema::analyzeIs(IsExpr *expr) {
    analyzeExpr(expr->value.get());
    TypeRef targetType = resolveTypeNode(expr->type.get());
    if (targetType &&
        (targetType->kind == TypeKindSem::Void || targetType->kind == TypeKindSem::Never ||
         targetType->kind == TypeKindSem::Module)) {
        error(expr->loc, "Cannot use '" + targetType->toDisplayString() + "' in an `is` check");
    }
    return types::boolean();
}

/// @brief Analyze a type cast expression (value as Type).
/// @param expr The as expression node.
/// @return The target type of the cast.
TypeRef Sema::analyzeAs(AsExpr *expr) {
    TypeRef sourceType = analyzeExpr(expr->value.get());
    TypeRef targetType = resolveTypeNode(expr->type.get());

    // Skip validation when types are unknown/unresolved
    if (!sourceType || !targetType || sourceType->kind == TypeKindSem::Unknown ||
        targetType->kind == TypeKindSem::Unknown)
        return targetType;

    // Check standard convertibility (numeric, string, assignment-compatible)
    if (sourceType->isConvertibleTo(*targetType))
        return targetType;

    TypeRef effectiveSource = sourceType;
    if (sourceType->kind == TypeKindSem::Optional && sourceType->innerType())
        effectiveSource = sourceType->innerType();

    // Allow class-to-class casts (downcasts and cross-casts for runtime checking)
    if (effectiveSource && effectiveSource->kind == TypeKindSem::Class &&
        targetType->kind == TypeKindSem::Class)
        return targetType;

    // Allow casts between object and interface references for runtime checking.
    if (effectiveSource &&
        (effectiveSource->kind == TypeKindSem::Class ||
         effectiveSource->kind == TypeKindSem::Interface) &&
        (targetType->kind == TypeKindSem::Class || targetType->kind == TypeKindSem::Interface)) {
        return targetType;
    }

    // Allow Ptr <-> Entity/Value interop (both are pointers at IL level)
    if ((sourceType->kind == TypeKindSem::Ptr &&
         (targetType->kind == TypeKindSem::Class || targetType->kind == TypeKindSem::Struct)) ||
        ((sourceType->kind == TypeKindSem::Class || sourceType->kind == TypeKindSem::Struct) &&
         targetType->kind == TypeKindSem::Ptr))
        return targetType;

    // Allow Optional[T] -> T (forced unwrap)
    if (sourceType->kind == TypeKindSem::Optional && !sourceType->typeArgs.empty() &&
        sourceType->typeArgs[0]->isConvertibleTo(*targetType))
        return targetType;

    error(expr->loc,
          "Cannot cast '" + sourceType->toDisplayString() + "' to '" +
              targetType->toDisplayString() + "'");
    return targetType;
}

/// @brief Analyze a range expression (start..end or start..<end).
/// @param expr The range expression node.
/// @return List[Integer] type representing the range.
TypeRef Sema::analyzeRange(RangeExpr *expr) {
    TypeRef startType = analyzeExpr(expr->start.get());
    TypeRef endType = analyzeExpr(expr->end.get());

    if (!startType->isIntegral() || !endType->isIntegral()) {
        error(expr->loc, "Range bounds must be integers");
    }

    // Range type is internal - used for iteration
    return types::list(types::integer());
}

//=============================================================================
// Pattern Matching
//=============================================================================

/// @brief Analyze a match arm pattern for type compatibility and exhaustiveness.
/// @param pattern The pattern to analyze.
/// @param scrutineeType The type being matched against.
/// @param coverage Track which values are covered for exhaustiveness checking.
/// @param bindings Output map of variable bindings introduced by the pattern.
/// @return True if the pattern is valid, false otherwise.
/// @details Handles wildcard, binding, literal, constructor, and tuple patterns.
bool Sema::analyzeMatchPattern(const MatchArm::Pattern &pattern,
                               TypeRef scrutineeType,
                               MatchCoverage &coverage,
                               std::unordered_map<std::string, TypeRef> &bindings) {
    auto bind = [&](const std::string &name, TypeRef type) {
        if (bindings.find(name) != bindings.end()) {
            error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                  "Duplicate binding name in pattern: " + name);
            return;
        }
        bindings[name] = type ? type : types::unknown();
    };

    switch (pattern.kind) {
        case MatchArm::Pattern::Kind::Wildcard:
            coverage.hasIrrefutable = true;
            return true;

        case MatchArm::Pattern::Kind::Binding:
            if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional &&
                pattern.binding == "None") {
                coverage.coversNull = true;
                return true;
            }

            bind(pattern.binding, scrutineeType);
            if (!pattern.guard)
                coverage.hasIrrefutable = true;
            return true;

        case MatchArm::Pattern::Kind::Literal: {
            if (pattern.literal) {
                TypeRef litType = analyzeExpr(pattern.literal.get());
                if (scrutineeType && !scrutineeType->isAssignableFrom(*litType)) {
                    error(pattern.literal->loc,
                          "Pattern literal type '" + litType->toDisplayString() +
                              "' is not compatible with scrutinee type '" +
                              scrutineeType->toDisplayString() + "'");
                }

                if (pattern.literal->kind == ExprKind::IntLiteral) {
                    coverage.coveredIntegers.insert(
                        static_cast<IntLiteralExpr *>(pattern.literal.get())->value);
                } else if (pattern.literal->kind == ExprKind::Unary) {
                    // Negative integer literal: -(IntLiteral)
                    auto *unary = static_cast<UnaryExpr *>(pattern.literal.get());
                    if (unary->op == UnaryOp::Neg && unary->operand &&
                        unary->operand->kind == ExprKind::IntLiteral) {
                        int64_t val = static_cast<IntLiteralExpr *>(unary->operand.get())->value;
                        coverage.coveredIntegers.insert(-val);
                    }
                } else if (pattern.literal->kind == ExprKind::BoolLiteral) {
                    coverage.coveredBooleans.insert(
                        static_cast<BoolLiteralExpr *>(pattern.literal.get())->value);
                } else if (pattern.literal->kind == ExprKind::NullLiteral) {
                    coverage.coversNull = true;
                } else if (pattern.literal->kind == ExprKind::Field && litType &&
                           litType->kind == TypeKindSem::Enum) {
                    auto *fieldExpr = static_cast<FieldExpr *>(pattern.literal.get());
                    coverage.coveredEnumVariants.insert(fieldExpr->field);
                }
            }
            return true;
        }

        case MatchArm::Pattern::Kind::Expression:
            if (pattern.literal) {
                TypeRef exprType = analyzeExpr(pattern.literal.get());
                if (exprType->kind != TypeKindSem::Boolean) {
                    error(pattern.literal->loc, "Match expression patterns must be Boolean");
                }
            }
            return true;

        case MatchArm::Pattern::Kind::Tuple: {
            if (!scrutineeType || scrutineeType->kind != TypeKindSem::Tuple) {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Tuple pattern requires tuple scrutinee");
                return false;
            }

            const auto &elements = scrutineeType->tupleElementTypes();
            if (elements.size() != pattern.subpatterns.size()) {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Tuple pattern arity mismatch");
                return false;
            }

            for (size_t i = 0; i < elements.size(); ++i) {
                analyzeMatchPattern(pattern.subpatterns[i], elements[i], coverage, bindings);
            }
            return true;
        }

        case MatchArm::Pattern::Kind::Constructor: {
            if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional) {
                if (pattern.binding == "Some") {
                    coverage.coversSome = true;
                    if (pattern.subpatterns.size() != 1) {
                        error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                              "Some() pattern requires exactly one subpattern");
                        return false;
                    }
                    TypeRef inner = scrutineeType->innerType();
                    analyzeMatchPattern(pattern.subpatterns[0], inner, coverage, bindings);
                    return true;
                }
                if (pattern.binding == "None") {
                    coverage.coversNull = true;
                    if (!pattern.subpatterns.empty()) {
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

            if (scrutineeType && scrutineeType->kind == TypeKindSem::Result) {
                if (pattern.binding == "Ok") {
                    coverage.coversResultOk = true;
                    if (pattern.subpatterns.size() != 1) {
                        error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                              "Ok() pattern requires exactly one subpattern");
                        return false;
                    }
                    TypeRef success = !scrutineeType->typeArgs.empty() ? scrutineeType->typeArgs[0]
                                                                       : types::unknown();
                    analyzeMatchPattern(pattern.subpatterns[0], success, coverage, bindings);
                    return true;
                }
                if (pattern.binding == "Err") {
                    coverage.coversResultErr = true;
                    if (pattern.subpatterns.size() != 1) {
                        error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                              "Err() pattern requires exactly one subpattern");
                        return false;
                    }
                    analyzeMatchPattern(
                        pattern.subpatterns[0], types::string(), coverage, bindings);
                    return true;
                }

                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Unknown Result constructor pattern: " + pattern.binding);
                return false;
            }

            if (!scrutineeType || (scrutineeType->kind != TypeKindSem::Struct &&
                                   scrutineeType->kind != TypeKindSem::Class)) {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern requires struct or class scrutinee");
                return false;
            }

            if (pattern.binding != scrutineeType->name) {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern '" + pattern.binding +
                          "' does not match scrutinee type '" + scrutineeType->name + "'");
                return false;
            }

            std::vector<TypeRef> fieldTypesVec;
            if (scrutineeType->kind == TypeKindSem::Struct) {
                auto it = structDecls_.find(scrutineeType->name);
                if (it != structDecls_.end()) {
                    for (auto &member : it->second->members) {
                        if (member->kind == DeclKind::Field) {
                            auto *field = static_cast<FieldDecl *>(member.get());
                            fieldTypesVec.push_back(field->type ? resolveTypeNode(field->type.get())
                                                                : types::unknown());
                        }
                    }
                }
            } else {
                auto it = classDecls_.find(scrutineeType->name);
                if (it != classDecls_.end()) {
                    for (auto &member : it->second->members) {
                        if (member->kind == DeclKind::Field) {
                            auto *field = static_cast<FieldDecl *>(member.get());
                            fieldTypesVec.push_back(field->type ? resolveTypeNode(field->type.get())
                                                                : types::unknown());
                        }
                    }
                }
            }

            if (fieldTypesVec.size() != pattern.subpatterns.size()) {
                error(pattern.literal ? pattern.literal->loc : SourceLoc{},
                      "Constructor pattern field arity mismatch");
                return false;
            }

            for (size_t i = 0; i < fieldTypesVec.size(); ++i) {
                analyzeMatchPattern(pattern.subpatterns[i], fieldTypesVec[i], coverage, bindings);
            }
            return true;
        }

        case MatchArm::Pattern::Kind::Or: {
            // OR pattern: validate each alternative against the scrutinee type.
            // No bindings are allowed in OR patterns (ambiguous which alternative bound).
            for (const auto &subpattern : pattern.subpatterns) {
                std::unordered_map<std::string, TypeRef> subBindings;
                analyzeMatchPattern(subpattern, scrutineeType, coverage, subBindings);

                if (!subBindings.empty()) {
                    error(subpattern.literal ? subpattern.literal->loc : SourceLoc{},
                          "Bindings are not allowed in OR patterns");
                }
            }
            return true;
        }
    }

    return false;
}

TypeRef Sema::analyzeMatchExpr(MatchExpr *expr) {
    TypeRef scrutineeType = analyzeExpr(expr->scrutinee.get());
    scrutineeType = declaredOptionalSurfaceType(expr->scrutinee.get(), scrutineeType);
    exprTypes_[expr->scrutinee.get()] = scrutineeType;

    MatchCoverage coverage;
    TypeRef resultType = nullptr;
    bool hasResultType = false;
    bool incompatibleResultType = false;

    for (auto &arm : expr->arms) {
        std::unordered_map<std::string, TypeRef> bindings;
        std::unordered_map<std::string, bool> bindingWasInitialized;
        pushScope(expr->loc);

        analyzeMatchPattern(arm.pattern, scrutineeType, coverage, bindings);

        for (const auto &binding : bindings) {
            Symbol sym;
            sym.kind = Symbol::Kind::Variable;
            sym.name = binding.first;
            sym.type = binding.second;
            sym.isFinal = true;
            defineSymbol(binding.first, sym, expr->loc);
            bindingWasInitialized.emplace(binding.first, isInitialized(binding.first));
            markInitialized(binding.first);
        }

        if (arm.pattern.guard) {
            TypeRef guardType = analyzeExpr(arm.pattern.guard.get());
            if (guardType->kind != TypeKindSem::Boolean) {
                error(arm.pattern.guard->loc, "Match guard must be Boolean");
            }
        }

        TypeRef bodyType = analyzeExpr(arm.body.get());
        if (!hasResultType) {
            resultType = bodyType;
            hasResultType = true;
        } else if (!incompatibleResultType) {
            TypeRef combined = commonType(resultType, bodyType);
            const bool knownPrior = resultType && resultType->kind != TypeKindSem::Unknown &&
                                    resultType->kind != TypeKindSem::Error;
            const bool knownBody = bodyType && bodyType->kind != TypeKindSem::Unknown &&
                                   bodyType->kind != TypeKindSem::Error;
            if (knownPrior && knownBody &&
                (!combined || combined->kind == TypeKindSem::Unknown ||
                 combined->kind == TypeKindSem::Error)) {
                error(arm.body ? arm.body->loc : expr->loc,
                      "Incompatible match arm type " + bodyType->toDisplayString() +
                          " with prior arm type " + resultType->toDisplayString());
                resultType = types::unknown();
                incompatibleResultType = true;
            } else {
                resultType = combined;
            }
        }

        popScope(arm.body ? arm.body->loc : expr->loc);
        for (const auto &[name, wasInitialized] : bindingWasInitialized) {
            if (!wasInitialized)
                initializedVars_.erase(name);
        }
    }

    if (!coverage.hasIrrefutable) {
        if (scrutineeType && scrutineeType->kind == TypeKindSem::Boolean) {
            if (coverage.coveredBooleans.size() < 2) {
                error(expr->loc,
                      "Non-exhaustive patterns: match on Boolean must cover both true "
                      "and false, or use a wildcard (_)");
            }
        } else if (scrutineeType && scrutineeType->kind == TypeKindSem::Enum) {
            auto it = enumDecls_.find(scrutineeType->name);
            if (it != enumDecls_.end()) {
                size_t totalVariants = it->second->variants.size();
                if (coverage.coveredEnumVariants.size() < totalVariants) {
                    std::string missing;
                    for (const auto &v : it->second->variants) {
                        if (coverage.coveredEnumVariants.find(v.name) ==
                            coverage.coveredEnumVariants.end()) {
                            if (!missing.empty())
                                missing += ", ";
                            missing += scrutineeType->name + "." + v.name;
                        }
                    }
                    error(expr->loc, "Non-exhaustive patterns: missing variants " + missing);
                }
            }
        } else if (scrutineeType && scrutineeType->isIntegral()) {
            error(expr->loc,
                  "Non-exhaustive patterns: match on Integer requires a wildcard (_) or "
                  "else case to be exhaustive");
        } else if (scrutineeType && scrutineeType->kind == TypeKindSem::Optional) {
            if (!(coverage.coversNull && coverage.coversSome)) {
                error(expr->loc,
                      "Non-exhaustive patterns: match on optional type should use a "
                      "wildcard (_) or handle all cases");
            }
        } else if (scrutineeType && scrutineeType->kind == TypeKindSem::Result) {
            if (!(coverage.coversResultOk && coverage.coversResultErr)) {
                error(expr->loc,
                      "Non-exhaustive patterns: match on Result should handle Ok and Err or use "
                      "a wildcard (_)");
            }
        } else {
            warn(WarningCode::W019_NonExhaustiveMatch,
                 expr->loc,
                 "Non-exhaustive patterns: consider adding a wildcard (_) case "
                 "to handle all possible values");
        }
    }

    return resultType ? resultType : types::unknown();
}

//=============================================================================
// New, Lambda, and Collection Literals
//=============================================================================

TypeRef Sema::analyzeNew(NewExpr *expr) {
    TypeRef type = resolveTypeNode(expr->type.get());
    if (!type) {
        for (auto &arg : expr->args)
            analyzeExpr(arg.value.get());
        return types::unknown();
    }

    auto findRuntimeCtor = [&](TypeRef candidate) -> Symbol * {
        if (!candidate || candidate->name.empty())
            return nullptr;

        if (expr->args.empty()) {
            std::string defaultCtorName = candidate->name + ".NewDefault";
            if (Symbol *sym = lookupSymbol(defaultCtorName);
                sym && sym->kind == Symbol::Kind::Function) {
                return sym;
            }
        }

        std::string ctorName = candidate->name + ".New";
        if (Symbol *sym = lookupSymbol(ctorName); sym && sym->kind == Symbol::Kind::Function)
            return sym;

        if (const auto *rtClass = il::runtime::findRuntimeClassByQName(candidate->name)) {
            if (rtClass->ctor) {
                if (Symbol *sym = lookupSymbol(rtClass->ctor);
                    sym && sym->kind == Symbol::Kind::Function) {
                    return sym;
                }
            }
        }

        return nullptr;
    };

    // Allow new for value/class types and collection types (List, Set, Map)
    bool allowed = type->kind == TypeKindSem::Struct || type->kind == TypeKindSem::Class ||
                   type->kind == TypeKindSem::List || type->kind == TypeKindSem::Set ||
                   type->kind == TypeKindSem::Map;

    // Also allow new for runtime classes that have a constructor
    if (!allowed && !type->name.empty()) {
        if (findRuntimeCtor(type)) {
            allowed = true;
        }
    }

    if (!allowed) {
        error(expr->loc, "'new' can only be used with struct, class, or collection types");
    }

    // Analyze constructor arguments
    for (auto &arg : expr->args) {
        analyzeExpr(arg.value.get());
    }

    if (type->kind != TypeKindSem::Struct && type->kind != TypeKindSem::Class) {
        Symbol *ctorSym = nullptr;
        if (!type->name.empty()) {
            ctorSym = findRuntimeCtor(type);
        }

        if (ctorSym && ctorSym->kind == Symbol::Kind::Function && ctorSym->isExtern) {
            CallArgBinding binding;
            auto specs = makeExternParamSpecs(*ctorSym);
            if (!bindCallArgs(expr->args,
                              specs,
                              expr->loc,
                              type->name + ".New",
                              binding,
                              nullptr,
                              true,
                              true)) {
                return types::unknown();
            }
            newArgBindings_[expr] = binding;
            return type;
        }

        for (const auto &arg : expr->args) {
            if (arg.name) {
                error(arg.value ? arg.value->loc : expr->loc,
                      "Named arguments are not supported for this constructor");
                return types::unknown();
            }
        }
        return type;
    }

    auto hasOwnInit = [&](const std::string &typeName, bool isClass) {
        if (isClass) {
            auto classIt = classDecls_.find(typeName);
            if (classIt == classDecls_.end())
                return false;
            for (const auto &member : classIt->second->members) {
                if (member->kind == DeclKind::Method &&
                    static_cast<MethodDecl *>(member.get())->name == "init") {
                    return true;
                }
            }
            return false;
        }

        auto structIt = structDecls_.find(typeName);
        if (structIt == structDecls_.end())
            return false;
        for (const auto &member : structIt->second->members) {
            if (member->kind == DeclKind::Method &&
                static_cast<MethodDecl *>(member.get())->name == "init") {
                return true;
            }
        }
        return false;
    };

    const bool hasInit = type->kind == TypeKindSem::Class ? hasOwnInit(type->name, true)
                                                          : hasOwnInit(type->name, false);

    if (hasInit) {
        std::string resolvedOwner;
        CallArgBinding binding;
        MethodDecl *initDecl = resolveMethodArgOverload(
            type->name, "init", expr->args, expr->loc, &resolvedOwner, false, &binding);

        if (!initDecl) {
            error(expr->loc,
                  "Type '" + type->name + "' has no init overload matching the provided arguments");
            return types::unknown();
        }

        resolvedInitDecls_[expr] = initDecl;
        resolvedInitOwnerTypes_[expr] = resolvedOwner;
        newArgBindings_[expr] = binding;
    } else {
        CallArgBinding binding;
        auto fieldSpecs = type->kind == TypeKindSem::Class ? makeClassFieldSpecs(type->name)
                                                           : makeStructFieldSpecs(type->name);
        if (!bindCallArgs(expr->args, fieldSpecs, expr->loc, type->name, binding, nullptr, true))
            return types::unknown();
        newArgBindings_[expr] = binding;
    }

    return type;
}

TypeRef Sema::analyzeLambda(LambdaExpr *expr) {
    // Collect names that are local to the lambda (params)
    std::set<std::string> lambdaLocals;
    for (const auto &param : expr->params) {
        lambdaLocals.insert(param.name);
    }

    pushScope(expr->loc);

    std::vector<TypeRef> paramTypes;
    for (const auto &param : expr->params) {
        if (!param.type) {
            error(expr->loc, "lambda parameters require explicit type annotations");
            paramTypes.push_back(types::unknown());

            Symbol sym;
            sym.kind = Symbol::Kind::Parameter;
            sym.name = param.name;
            sym.type = types::unknown();
            sym.isFinal = true;
            defineSymbol(param.name, sym, expr->loc);
            markInitialized(param.name);
            continue;
        }

        TypeRef paramType = resolveTypeNode(param.type.get());
        paramTypes.push_back(paramType);

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym, expr->loc);
        markInitialized(param.name);
    }

    TypeRef bodyType = analyzeExpr(expr->body.get());

    popScope(expr->body ? expr->body->loc : expr->loc);

    // Collect captured variables (free variables referenced in the body)
    collectCaptures(expr->body.get(), lambdaLocals, expr->captures);

    TypeRef returnType = expr->returnType ? resolveTypeNode(expr->returnType.get()) : bodyType;
    return types::function(paramTypes, returnType);
}

TypeRef Sema::analyzeListLiteral(ListLiteralExpr *expr) {
    TypeRef elementType = types::unknown();
    bool incompatible = false;

    for (auto &elem : expr->elements) {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elemType && elemType->kind == TypeKindSem::Unit) {
            error(elem->loc, "Unit literal cannot be stored in a List");
            incompatible = true;
            elemType = types::unknown();
        }
        TypeRef combined = commonType(elementType, elemType);
        if (elementType && elemType && elementType->kind != TypeKindSem::Unknown &&
            elemType->kind != TypeKindSem::Unknown && combined->kind == TypeKindSem::Unknown) {
            error(elem->loc,
                  "List literal contains incompatible element type " + elemType->toDisplayString() +
                      " with prior element type " + elementType->toDisplayString());
            incompatible = true;
        }
        elementType = combined;
    }

    if (incompatible)
        elementType = types::error();
    return types::list(elementType);
}

TypeRef Sema::analyzeMapLiteral(MapLiteralExpr *expr) {
    TypeRef keyType = types::string();
    TypeRef valueType = types::unknown();
    bool incompatible = false;

    for (auto &entry : expr->entries) {
        TypeRef kType = analyzeExpr(entry.key.get());
        TypeRef vType = analyzeExpr(entry.value.get());

        if (kType && kType->kind == TypeKindSem::Unit) {
            error(entry.key->loc, "Unit literal cannot be used as a Map key");
            kType = types::unknown();
        }
        if (vType && vType->kind == TypeKindSem::Unit) {
            error(entry.value->loc, "Unit literal cannot be stored in a Map");
            incompatible = true;
            vType = types::unknown();
        }

        if (kType->kind != TypeKindSem::String) {
            error(entry.key->loc, "Map keys must be String");
        }

        TypeRef combined = commonType(valueType, vType);
        if (valueType && vType && valueType->kind != TypeKindSem::Unknown &&
            vType->kind != TypeKindSem::Unknown && combined->kind == TypeKindSem::Unknown) {
            error(entry.value->loc,
                  "Map literal contains incompatible value type " + vType->toDisplayString() +
                      " with prior value type " + valueType->toDisplayString());
            incompatible = true;
        }
        valueType = combined;
    }

    if (incompatible)
        valueType = types::error();
    return types::map(keyType, valueType);
}

TypeRef Sema::analyzeSetLiteral(SetLiteralExpr *expr) {
    TypeRef elementType = types::unknown();
    bool incompatible = false;

    for (auto &elem : expr->elements) {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elemType && elemType->kind == TypeKindSem::Unit) {
            error(elem->loc, "Unit literal cannot be stored in a Set");
            incompatible = true;
            elemType = types::unknown();
        }
        TypeRef combined = commonType(elementType, elemType);
        if (elementType && elemType && elementType->kind != TypeKindSem::Unknown &&
            elemType->kind != TypeKindSem::Unknown && combined->kind == TypeKindSem::Unknown) {
            error(elem->loc,
                  "Set literal contains incompatible element type " + elemType->toDisplayString() +
                      " with prior element type " + elementType->toDisplayString());
            incompatible = true;
        }
        elementType = combined;
    }

    if (incompatible)
        elementType = types::error();
    return types::set(elementType);
}

//=============================================================================
// Tuple and Block Expressions
//=============================================================================

TypeRef Sema::analyzeTuple(TupleExpr *expr) {
    std::vector<TypeRef> elementTypes;
    for (auto &elem : expr->elements) {
        elementTypes.push_back(analyzeExpr(elem.get()));
    }
    return types::tuple(std::move(elementTypes));
}

TypeRef Sema::analyzeTupleIndex(TupleIndexExpr *expr) {
    TypeRef tupleType = analyzeExpr(expr->tuple.get());

    if (!tupleType->isTuple()) {
        error(expr->loc,
              "tuple index access requires a tuple type, got '" + tupleType->toDisplayString() +
                  "'");
        return types::unknown();
    }

    if (expr->index >= tupleType->tupleElementTypes().size()) {
        error(expr->loc,
              "tuple index " + std::to_string(expr->index) + " is out of bounds for " +
                  tupleType->toDisplayString());
        return types::unknown();
    }

    return tupleType->tupleElementType(expr->index);
}

TypeRef Sema::analyzeBlockExpr(BlockExpr *expr) {
    pushScope(expr->loc);

    // Analyze each statement in the block
    for (auto &stmt : expr->statements) {
        analyzeStmt(stmt.get());
    }

    // Analyze the final value expression if present
    TypeRef resultType = types::unit();
    if (expr->value) {
        resultType = analyzeExpr(expr->value.get());
    }

    SourceLoc endLoc = expr->value ? expr->value->loc : expr->loc;
    if (!expr->statements.empty())
        endLoc = scopeEndForStmt(expr->statements.back().get());
    popScope(endLoc);
    return resultType;
}

/// @brief Analyze a struct-literal expression (`TypeName { field = val, ... }`).
/// @param expr The struct-literal expression node.
/// @return The struct type named by the expression, or unknown on error.
TypeRef Sema::analyzeStructLiteral(StructLiteralExpr *expr) {
    // Look up the type name and verify it is a struct type.
    TypeRef valueType = resolveNamedType(expr->typeName, expr->loc);
    if (!valueType) {
        error(expr->loc, "Unknown type '" + expr->typeName + "'");
        return types::unknown();
    }
    if (!valueType || valueType->kind != TypeKindSem::Struct) {
        error(expr->loc,
              "'" + expr->typeName +
                  "' is not a struct type; struct literal requires a struct type");
        return types::unknown();
    }

    // For each named field, verify the field exists once and type-check the value.
    std::unordered_set<std::string> seenFields;
    for (auto &field : expr->fields) {
        if (!seenFields.insert(field.name).second) {
            error(field.loc,
                  "Duplicate field '" + field.name + "' in struct literal for '" + expr->typeName +
                      "'");
            continue;
        }

        const std::string ownerName = valueType->name;
        TypeRef fieldType = getFieldType(ownerName, field.name);
        if (!fieldType) {
            error(field.loc, "'" + expr->typeName + "' has no field '" + field.name + "'");
            continue;
        }
        auto visIt = memberVisibility_.find(ownerName + "." + field.name);
        if (visIt != memberVisibility_.end() && visIt->second == Visibility::Private &&
            (!currentSelfType_ || currentSelfType_->name != ownerName)) {
            error(field.loc,
                  "Field '" + ownerName + "." + field.name +
                      "' is private and cannot be initialized here");
            continue;
        }
        TypeRef valType = analyzeExpr(field.value.get());
        if (!valType || valType->kind == TypeKindSem::Unknown ||
            valType->kind == TypeKindSem::Error || fieldType->kind == TypeKindSem::Unknown ||
            fieldType->kind == TypeKindSem::Error)
            continue;
        if (!fieldType->isAssignableFrom(*valType))
            errorTypeMismatch(field.loc, fieldType, valType);
    }

    return valueType;
}

} // namespace il::frontends::zia
