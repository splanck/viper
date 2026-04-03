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
            // Try to find a getter function: {ClassName}.get_{PropertyName}
            std::string getterName = dottedBase + ".get_" + expr->field;
            Symbol *sym = lookupSymbol(getterName);
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
            sym = lookupSymbol(funcName);
            if (sym && sym->kind == Symbol::Kind::Function) {
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

    // Unwrap Optional types for field/method access with null-safety warning.
    // Without flow-sensitive null analysis, we cannot verify a null check
    // precedes this access, so emit a warning for potential null dereference.
    if (baseType && baseType->kind == TypeKindSem::Optional && baseType->innerType()) {
        warn(WarningCode::W016_OptionalWithoutCheck,
             expr->loc,
             "Accessing member '" + expr->field + "' on Optional type '" + baseType->toString() +
                 "' without null check");
        baseType = baseType->innerType();
    }

    // Handle module-qualified access (e.g., colors.initColors or Canvas.New)
    if (baseType && baseType->kind == TypeKindSem::Module) {
        // Build the full qualified name (e.g., Viper.Graphics.Canvas.New)
        std::string fullName = baseType->name + "." + expr->field;

        // First try to look up the qualified name directly
        Symbol *sym = lookupSymbol(fullName);
        if (sym) {
            return sym->type;
        }

        // For runtime classes (Viper.*), the symbol might not be in the symbol table
        // but could be a valid runtime method. Check importedSymbols_ for the method.
        auto importIt = importedSymbols_.find(fullName);
        if (importIt != importedSymbols_.end()) {
            return types::module(importIt->second);
        }

        // For local modules, also try unqualified name (for backwards compatibility)
        sym = lookupSymbol(expr->field);
        if (sym) {
            return sym->type;
        }

        // Check if fullName is a valid runtime class or sub-namespace
        // (e.g., "Viper.Collections.List" when accessed via alias "Collections.List")
        if (isValidRuntimeNamespace(fullName)) {
            return types::module(fullName);
        }

        // Also check typeRegistry_ for the qualified name directly
        // (handles runtime types that are registered but not in importedSymbols_)
        auto typeIt = typeRegistry_.find(fullName);
        if (typeIt != typeRegistry_.end()) {
            return typeIt->second;
        }

        // Try the getter convention (get_PropertyName) for static properties
        // on runtime classes. Properties like Color.RED are registered as
        // "Viper.Graphics.Color.get_RED" in the symbol table.
        {
            std::string getterName = baseType->name + ".get_" + expr->field;
            Symbol *getter = lookupSymbol(getterName);
            if (getter && getter->kind == Symbol::Kind::Function) {
                // Record the getter so the lowerer emits a call (same as Path A)
                resolvedFieldGetters_[expr] = getterName;
                TypeRef funcType = getter->type;
                if (funcType && funcType->kind == TypeKindSem::Function)
                    return normalizeRuntimeSurfaceType(funcType->returnType());
                return normalizeRuntimeSurfaceType(funcType);
            }
        }

        // If not found in global scope, report error
        error(expr->loc,
              "Module '" + baseType->name + "' has no exported symbol '" + expr->field + "'");
        return types::unknown();
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

    // Check if this is a field or method access on a value or class type
    if (baseType &&
        (baseType->kind == TypeKindSem::Struct || baseType->kind == TypeKindSem::Class)) {
        std::string memberKey = baseType->name + "." + expr->field;

        // Check if accessing from inside or outside the type
        bool isInsideType = currentSelfType_ && currentSelfType_->name == baseType->name;

        // Check visibility
        auto visIt = memberVisibility_.find(memberKey);
        if (visIt != memberVisibility_.end()) {
            if (visIt->second == Visibility::Private && !isInsideType) {
                error(expr->loc,
                      "Cannot access private member '" + expr->field + "' of type '" +
                          baseType->name + "'");
            }
        }

        // Check if it's a method
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
                      "Cannot access private member '" + expr->field + "' of type '" +
                          baseType->name + "'");
                return types::unknown();
            }
            TypeRef methodType = getMethodType(baseType->name, method);
            if (methodType)
                return methodType;
        }

        // Check if it's a field
        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end()) {
            return fieldIt->second;
        }

        if (const PropertyDecl *prop = findPropertyDecl(baseType->name, expr->field)) {
            if (prop->visibility == Visibility::Private && !isInsideType) {
                error(expr->loc,
                      "Cannot access private member '" + expr->field + "' of type '" +
                          baseType->name + "'");
                return types::unknown();
            }

            resolvedFieldGetters_[expr] = baseType->name + ".get_" + prop->name;
            return prop->type ? resolveTypeNode(prop->type.get()) : types::unknown();
        }

        // Field/method not found on this class or struct type
        error(expr->loc, "Type '" + baseType->name + "' has no member '" + expr->field + "'");
        return types::unknown();
    }

    // Handle built-in properties like .Length on lists
    if (baseType && baseType->kind == TypeKindSem::List) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
    }

    // Handle built-in properties on maps (.Length, .Len, .Count, etc.)
    if (baseType && baseType->kind == TypeKindSem::Map) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
    }

    // Handle built-in properties on sets (.Length, .Len, .Count, etc.)
    if (baseType && baseType->kind == TypeKindSem::Set) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            return types::integer();
        }
    }

    // Handle built-in properties on strings (Bug #3 fix)
    if (baseType && baseType->kind == TypeKindSem::String) {
        if (expr->field == "Length" || expr->field == "length") {
            return types::integer();
        }
    }

    // Reject field access on primitive types that have no members
    if (baseType &&
        (baseType->kind == TypeKindSem::Integer || baseType->kind == TypeKindSem::Number ||
         baseType->kind == TypeKindSem::Boolean || baseType->kind == TypeKindSem::Byte)) {
        error(expr->loc, "Type '" + baseType->toString() + "' has no member '" + expr->field + "'");
        return types::unknown();
    }

    // Handle runtime class property access (e.g., app.Root, editor.LineCount)
    // Runtime classes are Ptr types with a name like "Viper.GUI.App"
    if (baseType && baseType->kind == TypeKindSem::Ptr && !baseType->name.empty() &&
        baseType->name.find("Viper.") == 0) {
        const auto &registry = il::runtime::RuntimeRegistry::instance();
        std::string getterName = baseType->name + ".get_" + expr->field;
        if (auto prop = registry.findProperty(baseType->name, expr->field);
            prop && prop->getter && *prop->getter) {
            getterName = prop->getter;
        }

        Symbol *sym = lookupSymbol(getterName);
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

//=============================================================================
// Optional and Type Operators
//=============================================================================

TypeRef Sema::analyzeForceUnwrap(ForceUnwrapExpr *expr) {
    TypeRef operandType = analyzeExpr(expr->operand.get());

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
                  (operandType ? operandType->toString() : "unknown"));
        return operandType ? operandType : types::unknown();
    }

    TypeRef inner = operandType->innerType();
    return inner ? inner : types::unknown();
}

TypeRef Sema::analyzeOptionalChain(OptionalChainExpr *expr) {
    TypeRef baseType = analyzeExpr(expr->base.get());

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
        auto fieldIt = fieldTypes_.find(memberKey);
        if (fieldIt != fieldTypes_.end()) {
            fieldType = fieldIt->second;
        } else {
            error(expr->loc,
                  "Unknown field '" + expr->field + "' on type '" + innerType->name + "'");
        }
    } else if (innerType->kind == TypeKindSem::List) {
        if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on List");
        }
    } else if (innerType->kind == TypeKindSem::Map) {
        if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on Map");
        }
    } else if (innerType->kind == TypeKindSem::Set) {
        if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
            fieldType = types::integer();
        } else {
            error(expr->loc, "Unknown field '" + expr->field + "' on Set");
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
    TypeRef rightType = analyzeExpr(expr->right.get());

    // If left is non-optional (e.g. after flow-sensitive narrowing),
    // ?? is a no-op — just return the left type.
    if (leftType->kind != TypeKindSem::Optional) {
        return leftType;
    }

    // Result is the unwrapped type
    TypeRef innerType = leftType->innerType();
    return innerType ? innerType : rightType;
}

/// @brief Analyze a type check expression (value is Type).
/// @param expr The is expression node.
/// @return Boolean type (result of type check).
TypeRef Sema::analyzeIs(IsExpr *expr) {
    analyzeExpr(expr->value.get());
    resolveTypeNode(expr->type.get());
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

    // Allow class-to-class casts (downcasts and cross-casts for runtime checking)
    if (sourceType->kind == TypeKindSem::Class && targetType->kind == TypeKindSem::Class)
        return targetType;

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
          "Cannot cast '" + sourceType->toString() + "' to '" + targetType->toString() + "'");
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
                          "Pattern literal type '" + litType->toString() +
                              "' is not compatible with scrutinee type '" +
                              scrutineeType->toString() + "'");
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

    MatchCoverage coverage;
    TypeRef resultType = nullptr;

    for (auto &arm : expr->arms) {
        std::unordered_map<std::string, TypeRef> bindings;
        pushScope(expr->loc);

        analyzeMatchPattern(arm.pattern, scrutineeType, coverage, bindings);

        for (const auto &binding : bindings) {
            Symbol sym;
            sym.kind = Symbol::Kind::Variable;
            sym.name = binding.first;
            sym.type = binding.second;
            sym.isFinal = true;
            defineSymbol(binding.first, sym, expr->loc);
        }

        if (arm.pattern.guard) {
            TypeRef guardType = analyzeExpr(arm.pattern.guard.get());
            if (guardType->kind != TypeKindSem::Boolean) {
                error(arm.pattern.guard->loc, "Match guard must be Boolean");
            }
        }

        TypeRef bodyType = analyzeExpr(arm.body.get());
        resultType = commonType(resultType, bodyType);

        popScope(arm.body ? arm.body->loc : expr->loc);
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

    // Allow new for value/class types and collection types (List, Set, Map)
    bool allowed = type->kind == TypeKindSem::Struct || type->kind == TypeKindSem::Class ||
                   type->kind == TypeKindSem::List || type->kind == TypeKindSem::Set ||
                   type->kind == TypeKindSem::Map;

    // Also allow new for runtime classes that have a constructor
    if (!allowed && type && !type->name.empty()) {
        // First try the conventional .New suffix
        std::string ctorName = type->name + ".New";
        Symbol *sym = lookupSymbol(ctorName);
        if (sym && sym->kind == Symbol::Kind::Function) {
            allowed = true;
        } else {
            // Fall back to looking up the actual ctor from RuntimeRegistry catalog.
            // The ctor field is already a fully-qualified extern target, e.g.,
            // "Viper.Collections.FrozenSet.FromSeq"
            if (const auto *rtClass = il::runtime::findRuntimeClassByQName(type->name)) {
                if (rtClass->ctor) {
                    sym = lookupSymbol(rtClass->ctor);
                    if (sym && sym->kind == Symbol::Kind::Function) {
                        allowed = true;
                    }
                }
            }
        }
    }

    if (!allowed) {
        error(expr->loc, "'new' can only be used with struct, class, or collection types");
    }

    std::vector<TypeRef> argTypes;
    argTypes.reserve(expr->args.size());

    // Analyze constructor arguments
    for (auto &arg : expr->args) {
        argTypes.push_back(analyzeExpr(arg.value.get()));
    }

    // For class types, validate that arguments match an explicit init overload on the class.
    if (type->kind == TypeKindSem::Class) {
        MethodDecl *initDecl = resolveMethodOverload(
            type->name, "init", argTypes, expr->loc, nullptr, /*includeInherited=*/true);

        if (initDecl) {
            resolvedInitDecls_[expr] = initDecl;
        } else if (!expr->args.empty()) {
            error(expr->loc,
                  "Entity '" + type->name +
                      "' has no init overload matching the provided arguments");
        }
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
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
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

    for (auto &elem : expr->elements) {
        TypeRef elemType = analyzeExpr(elem.get());
        elementType = commonType(elementType, elemType);
    }

    return types::list(elementType);
}

TypeRef Sema::analyzeMapLiteral(MapLiteralExpr *expr) {
    TypeRef keyType = types::string();
    TypeRef valueType = types::unknown();

    for (auto &entry : expr->entries) {
        TypeRef kType = analyzeExpr(entry.key.get());
        TypeRef vType = analyzeExpr(entry.value.get());

        if (kType->kind != TypeKindSem::String) {
            error(entry.key->loc, "Map keys must be String");
        }

        valueType = commonType(valueType, vType);
    }

    return types::map(keyType, valueType);
}

TypeRef Sema::analyzeSetLiteral(SetLiteralExpr *expr) {
    TypeRef elementType = types::unknown();

    for (auto &elem : expr->elements) {
        TypeRef elemType = analyzeExpr(elem.get());
        if (elementType->kind == TypeKindSem::Unknown) {
            elementType = elemType;
        }
    }

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
              "tuple index access requires a tuple type, got '" + tupleType->toString() + "'");
        return types::unknown();
    }

    if (expr->index >= tupleType->tupleElementTypes().size()) {
        error(expr->loc,
              "tuple index " + std::to_string(expr->index) + " is out of bounds for " +
                  tupleType->toString());
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
    auto typeIt = typeRegistry_.find(expr->typeName);
    if (typeIt == typeRegistry_.end()) {
        error(expr->loc, "Unknown type '" + expr->typeName + "'");
        return types::unknown();
    }
    TypeRef valueType = typeIt->second;
    if (!valueType || valueType->kind != TypeKindSem::Struct) {
        error(expr->loc,
              "'" + expr->typeName + "' is not a struct type; struct literal requires a struct type");
        return types::unknown();
    }

    // For each named field, verify the field exists and type-check the value.
    for (auto &field : expr->fields) {
        TypeRef fieldType = getFieldType(expr->typeName, field.name);
        if (!fieldType) {
            error(field.loc, "'" + expr->typeName + "' has no field '" + field.name + "'");
            continue;
        }
        TypeRef valType = analyzeExpr(field.value.get());
        (void)valType; // type compatibility checked by assignment sema
    }

    return valueType;
}

} // namespace il::frontends::zia
