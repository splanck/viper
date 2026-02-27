//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema.cpp
/// @brief Implementation of Zia semantic analyzer.
///
/// @details This file implements the core Sema class: constructor, multi-pass
/// analyze() orchestrator, scope management, type narrowing, error reporting,
/// built-in registration, and namespace support.
///
/// Other Sema method groups are split into separate files:
/// - Sema_Generics.cpp: Generic type/function substitution and instantiation
/// - Sema_TypeResolution.cpp: Type resolution, extern functions, captures
/// - Sema_Runtime.cpp: Runtime function registration
/// - Sema_Decl.cpp: Declaration analysis (bind, value, entity, interface, etc.)
/// - Sema_Stmt.cpp: Statement analysis
/// - Sema_Expr.cpp: Expression analysis
///
/// @see Sema.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include <cassert>
#include <sstream>

namespace il::frontends::zia
{

//=============================================================================
// Scope Implementation
//=============================================================================

/// @brief Define a symbol in the current scope.
/// @param name The symbol name to register.
/// @param symbol The symbol metadata to associate with the name.
void Scope::define(const std::string &name, Symbol symbol)
{
    symbols_[name] = std::move(symbol);
}

/// @brief Look up a symbol by name, walking parent scopes.
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found, nullptr otherwise.
Symbol *Scope::lookup(const std::string &name)
{
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    if (parent_)
        return parent_->lookup(name);
    return nullptr;
}

/// @brief Look up a symbol only in the current scope (no parent walk).
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found in this scope, nullptr otherwise.
Symbol *Scope::lookupLocal(const std::string &name)
{
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

//=============================================================================
// Sema Implementation
//=============================================================================

Sema::Sema(il::support::DiagnosticEngine &diag) : diag_(diag)
{
    scopes_.push_back(std::make_unique<Scope>());
    currentScope_ = scopes_.back().get();
    types::clearInterfaceImplementations();
    registerBuiltins();
}

void Sema::initWarnings(const WarningPolicy &policy, std::string_view source)
{
    warningPolicy_ = &policy;
    suppressions_.scan(source);
}

/// @brief Run multi-pass semantic analysis on a module.
/// @details Pass 1: Register all top-level declarations (types, functions, globals).
///          Pass 1b: Process namespace declarations (recursive multi-pass).
///          Pass 2: Register member signatures (fields, method types) for type declarations.
///          Pass 3: Analyze declaration bodies (function bodies, method bodies, initializers).
/// @param module The module AST to analyze.
/// @return True if analysis succeeded without errors, false otherwise.
bool Sema::analyze(ModuleDecl &module)
{
    currentModule_ = &module;

    for (auto &bind : module.binds)
    {
        analyzeBind(bind);
    }

    // First pass: register all top-level declarations
    for (auto &decl : module.declarations)
    {
        switch (decl->kind)
        {
            case DeclKind::Function:
            {
                auto *func = static_cast<FunctionDecl *>(decl.get());

                if (!func->genericParams.empty())
                {
                    // Generic function: register for later instantiation
                    registerGenericFunction(func->name, func);

                    // Create a placeholder type with type parameters as param types
                    // The actual function type will be created when instantiated
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : func->genericParams)
                    {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    auto placeholderType = types::function(paramTypes, types::unknown());

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = func->name;
                    sym.type = placeholderType;
                    sym.decl = func;
                    defineSymbol(func->name, sym);
                }
                else
                {
                    // Non-generic function: resolve types normally
                    TypeRef returnType = func->returnType ? resolveTypeNode(func->returnType.get())
                                                          : types::voidType();

                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : func->params)
                    {
                        paramTypes.push_back(param.type ? resolveTypeNode(param.type.get())
                                                        : types::unknown());
                    }
                    auto funcType = types::function(paramTypes, returnType);

                    Symbol sym;
                    sym.kind = Symbol::Kind::Function;
                    sym.name = func->name;
                    sym.type = funcType;
                    sym.decl = func;
                    defineSymbol(func->name, sym);
                }
                break;
            }
            case DeclKind::Value:
            {
                auto *value = static_cast<ValueDecl *>(decl.get());
                valueDecls_[value->name] = value;

                TypeRef valueType;
                if (!value->genericParams.empty())
                {
                    // Generic type: register for later instantiation
                    registerGenericType(value->name, value);
                    // Create uninstantiated type placeholder with type parameters
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : value->genericParams)
                    {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    valueType =
                        std::make_shared<ViperType>(TypeKindSem::Value, value->name, paramTypes);
                }
                else
                {
                    valueType = types::value(value->name);
                }
                typeRegistry_[value->name] = valueType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = value->name;
                sym.type = valueType;
                sym.decl = value;
                defineSymbol(value->name, sym);
                break;
            }
            case DeclKind::Entity:
            {
                auto *entity = static_cast<EntityDecl *>(decl.get());
                entityDecls_[entity->name] = entity;

                TypeRef entityType;
                if (!entity->genericParams.empty())
                {
                    // Generic type: register for later instantiation
                    registerGenericType(entity->name, entity);
                    // Create uninstantiated type placeholder with type parameters
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : entity->genericParams)
                    {
                        paramTypes.push_back(types::typeParam(param));
                    }
                    entityType =
                        std::make_shared<ViperType>(TypeKindSem::Entity, entity->name, paramTypes);
                }
                else
                {
                    entityType = types::entity(entity->name);
                }
                typeRegistry_[entity->name] = entityType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = entity->name;
                sym.type = entityType;
                sym.decl = entity;
                defineSymbol(entity->name, sym);
                break;
            }
            case DeclKind::Interface:
            {
                auto *iface = static_cast<InterfaceDecl *>(decl.get());
                interfaceDecls_[iface->name] = iface;
                auto ifaceType = types::interface(iface->name);
                typeRegistry_[iface->name] = ifaceType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = iface->name;
                sym.type = ifaceType;
                sym.decl = iface;
                defineSymbol(iface->name, sym);
                break;
            }
            case DeclKind::GlobalVar:
            {
                auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
                // Determine the variable type
                TypeRef varType;
                if (gvar->type)
                {
                    varType = resolveTypeNode(gvar->type.get());
                }
                else if (gvar->initializer)
                {
                    // Type inference from initializer - defer to second pass
                    varType = types::unknown();
                }
                else
                {
                    varType = types::unknown();
                }

                Symbol sym;
                sym.kind = Symbol::Kind::Variable;
                sym.name = gvar->name;
                sym.type = varType;
                sym.isFinal = gvar->isFinal;
                sym.decl = gvar;
                defineSymbol(gvar->name, sym);
                // Global variables are always considered initialized
                // (either explicitly or default-initialized)
                markInitialized(gvar->name);
                break;
            }
            case DeclKind::Namespace:
            {
                // Namespaces are processed in a separate pass to handle their
                // nested declarations properly
                break;
            }
            default:
                break;
        }
    }

    // Process namespace declarations (they handle their own multi-pass analysis)
    for (auto &decl : module.declarations)
    {
        if (decl->kind == DeclKind::Namespace)
        {
            analyzeNamespaceDecl(*static_cast<NamespaceDecl *>(decl.get()));
        }
    }

    // Pre-pass: eagerly resolve types of final constants from literal initializers
    // This allows forward references to final constants in entity/function bodies
    registerFinalConstantTypes(module.declarations);

    // Second pass: register all method/field signatures (before analyzing bodies)
    // This ensures cross-module method calls can be resolved regardless of declaration order
    registerMemberSignatures(module.declarations);

    // Third pass: analyze declarations (bodies)
    analyzeDeclarationBodies(module.declarations);

    return !hasError_;
}

/// @brief Get the resolved semantic type of an expression.
/// @details Returns the cached type from exprTypes_, applying type parameter
///          substitution if currently in a generic context.
/// @param expr The expression to query.
/// @return The resolved type, or unknown() if the expression has not been analyzed.
TypeRef Sema::typeOf(const Expr *expr) const
{
    auto it = exprTypes_.find(expr);
    if (it == exprTypes_.end())
        return types::unknown();
    // Apply type parameter substitution if in generic context
    return substituteTypeParams(it->second);
}

/// @brief Resolve a type AST node to a semantic type reference.
/// @param node The type node to resolve.
/// @return The resolved semantic type.
TypeRef Sema::resolveType(const TypeNode *node) const
{
    return const_cast<Sema *>(this)->resolveTypeNode(node);
}

//=============================================================================
// Scope Management
//=============================================================================

/// @brief Push a new child scope onto the scope stack.
void Sema::pushScope()
{
    scopes_.push_back(std::make_unique<Scope>(currentScope_));
    currentScope_ = scopes_.back().get();
}

/// @brief Pop the current scope, restoring its parent as the active scope.
/// @pre There must be more than the global scope remaining.
/// @details Checks for unused variables (W001) in the scope before popping.
void Sema::popScope()
{
    assert(scopes_.size() > 1 && "cannot pop global scope");

    // W001: Check for unused variables/parameters in the scope being popped
    checkUnusedVariables(*currentScope_);

    currentScope_ = currentScope_->parent();
    scopes_.pop_back();
    assert(currentScope_ == scopes_.back().get() && "scope stack corrupted");
}

/// @brief Define a symbol in the current scope.
/// @param name The symbol name to register.
/// @param symbol The symbol metadata to associate with the name.
void Sema::defineSymbol(const std::string &name, Symbol symbol)
{
    currentScope_->define(name, std::move(symbol));
}

/// @brief Look up a symbol by name in the current scope chain.
/// @param name The symbol name to search for.
/// @return Pointer to the symbol if found, nullptr otherwise.
Symbol *Sema::lookupSymbol(const std::string &name)
{
    return currentScope_->lookup(name);
}

/// @brief Look up the type of a variable, respecting flow-sensitive type narrowing.
/// @details Checks narrowed types first (from null-check analysis), then falls back
///          to the declared type in scope.
/// @param name The variable name to look up.
/// @return The narrowed or declared type, or nullptr if not found.
TypeRef Sema::lookupVarType(const std::string &name)
{
    // Check narrowed types first (for flow-sensitive type analysis)
    for (auto it = narrowedTypes_.rbegin(); it != narrowedTypes_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
        {
            return found->second;
        }
    }

    // Fall back to declared type
    Symbol *sym = currentScope_->lookup(name);
    if (sym && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter))
    {
        return sym->type;
    }
    return nullptr;
}

//=============================================================================
// Type Narrowing (Flow-Sensitive Type Analysis)
//=============================================================================

/// @brief Push a new type narrowing scope for flow-sensitive analysis.
void Sema::pushNarrowingScope()
{
    narrowedTypes_.push_back({});
}

/// @brief Pop the current type narrowing scope.
void Sema::popNarrowingScope()
{
    if (!narrowedTypes_.empty())
    {
        narrowedTypes_.pop_back();
    }
}

/// @brief Narrow the type of a variable in the current narrowing scope.
/// @param name The variable whose type is being narrowed.
/// @param narrowedType The narrowed type to record.
void Sema::narrowType(const std::string &name, TypeRef narrowedType)
{
    if (!narrowedTypes_.empty())
    {
        narrowedTypes_.back()[name] = narrowedType;
    }
}

/// @brief Mark a variable as definitely initialized.
void Sema::markInitialized(const std::string &name)
{
    initializedVars_.insert(name);
}

/// @brief Check if a variable has been definitely initialized.
bool Sema::isInitialized(const std::string &name) const
{
    return initializedVars_.count(name) > 0;
}

/// @brief Save the current initialization state for branching analysis.
std::unordered_set<std::string> Sema::saveInitState() const
{
    return initializedVars_;
}

/// @brief Intersect two branch initialization states.
/// Only variables initialized in BOTH branches remain initialized.
void Sema::intersectInitState(const std::unordered_set<std::string> &branchA,
                              const std::unordered_set<std::string> &branchB)
{
    std::unordered_set<std::string> result;
    for (const auto &name : branchA)
    {
        if (branchB.count(name) > 0)
            result.insert(name);
    }
    initializedVars_ = std::move(result);
}

/// @brief Try to extract a null-check pattern from a condition expression.
/// @details Recognizes patterns: x != null, x == null, null != x, null == x.
/// @param[in] cond The condition expression to analyze.
/// @param[out] varName The variable name being null-checked.
/// @param[out] isNotNull True if the pattern is != null, false if == null.
/// @return True if a null-check pattern was recognized.
bool Sema::tryExtractNullCheck(Expr *cond, std::string &varName, bool &isNotNull)
{
    // Pattern: x != null or x == null
    if (cond->kind != ExprKind::Binary)
        return false;

    auto *binary = static_cast<BinaryExpr *>(cond);
    if (binary->op != BinaryOp::Ne && binary->op != BinaryOp::Eq)
        return false;

    isNotNull = (binary->op == BinaryOp::Ne);

    // Check for "x != null" pattern
    if (binary->left->kind == ExprKind::Ident && binary->right->kind == ExprKind::NullLiteral)
    {
        varName = static_cast<IdentExpr *>(binary->left.get())->name;
        return true;
    }

    // Check for "null != x" pattern
    if (binary->left->kind == ExprKind::NullLiteral && binary->right->kind == ExprKind::Ident)
    {
        varName = static_cast<IdentExpr *>(binary->right.get())->name;
        return true;
    }

    return false;
}

//=============================================================================
// Error Reporting
//=============================================================================

/// @brief Report a semantic warning at a source location (legacy).
void Sema::warning(SourceLoc loc, const std::string &message)
{
    diag_.report({il::support::Severity::Warning, message, loc, "V3001"});
}

/// @brief Report a coded warning with policy and suppression checks.
void Sema::warn(WarningCode code, SourceLoc loc, const std::string &message)
{
    // Check policy: is this warning enabled?
    if (warningPolicy_)
    {
        if (!warningPolicy_->isEnabled(code))
            return;
    }
    else
    {
        // No policy set — use default conservative set
        if (WarningPolicy::defaultEnabled().count(code) == 0)
            return;
    }

    // Check inline suppression
    if (suppressions_.isSuppressed(code, loc.line))
        return;

    // Determine severity: Warning or Error (if -Werror)
    auto sev = (warningPolicy_ && warningPolicy_->warningsAsErrors)
                   ? il::support::Severity::Error
                   : il::support::Severity::Warning;

    if (sev == il::support::Severity::Error)
        hasError_ = true;

    diag_.report({sev, message, loc, warningCodeStr(code)});
}

/// @brief Check for unused variables in a scope and emit W001 warnings.
void Sema::checkUnusedVariables(const Scope &scope)
{
    for (const auto &[name, sym] : scope.getSymbols())
    {
        // Only check variables and parameters
        if (sym.kind != Symbol::Kind::Variable && sym.kind != Symbol::Kind::Parameter)
            continue;

        // Skip the discard name "_"
        if (name == "_")
            continue;

        // Skip extern/runtime symbols
        if (sym.isExtern)
            continue;

        if (!sym.used)
        {
            std::string what = (sym.kind == Symbol::Kind::Parameter) ? "Parameter" : "Variable";
            warn(WarningCode::W001_UnusedVariable, sym.decl ? sym.decl->loc : SourceLoc{},
                 what + " '" + name + "' is declared but never used");
        }
    }
}

/// @brief Report a semantic error at a source location.
void Sema::error(SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, "V3000"});
}

/// @brief Report an "undefined identifier" error for the given name.
void Sema::errorUndefined(SourceLoc loc, const std::string &name)
{
    error(loc, "Undefined identifier: " + name);
}

/// @brief Report a type mismatch error showing expected vs actual types.
void Sema::errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual)
{
    error(loc, "Type mismatch: expected " + expected->toString() + ", got " + actual->toString());
}

//=============================================================================
// Built-in Functions
//=============================================================================

/// @brief Register built-in functions and runtime library functions.
/// @details Registers print, println, input, toString as built-in symbols,
///          then loads all Viper.* runtime functions from runtime.def.
void Sema::registerBuiltins()
{
    // print(String) -> Void
    {
        auto printType = types::function({types::string()}, types::voidType());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "print";
        sym.type = printType;
        defineSymbol("print", sym);
    }

    // println(String) -> Void (alias for print with newline)
    {
        auto printlnType = types::function({types::string()}, types::voidType());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "println";
        sym.type = printlnType;
        defineSymbol("println", sym);
    }

    // input() -> String
    {
        auto inputType = types::function({}, types::string());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "input";
        sym.type = inputType;
        defineSymbol("input", sym);
    }

    // toString(Any) -> String
    {
        auto toStringType = types::function({types::any()}, types::string());
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = "toString";
        sym.type = toStringType;
        defineSymbol("toString", sym);
    }

    // Register all Viper.* runtime functions from runtime.def
    // Generated from src/il/runtime/runtime.def (1002 functions)
    initRuntimeFunctions();
}

//===----------------------------------------------------------------------===//
// Namespace Support
//===----------------------------------------------------------------------===//

/// @brief Qualify a name with the current namespace prefix.
/// @param name The unqualified name.
/// @return The qualified name (prefix.name), or the original name if no namespace is active.
std::string Sema::qualifyName(const std::string &name) const
{
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

/// @brief Pass 2: Register member signatures (fields, methods) for type declarations.
/// @param declarations The declaration list to process.
void Sema::registerMemberSignatures(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        switch (decl->kind)
        {
            case DeclKind::Value:
                registerValueMembers(*static_cast<ValueDecl *>(decl.get()));
                break;
            case DeclKind::Entity:
                registerEntityMembers(*static_cast<EntityDecl *>(decl.get()));
                break;
            case DeclKind::Interface:
                registerInterfaceMembers(*static_cast<InterfaceDecl *>(decl.get()));
                break;
            default:
                break;
        }
    }
}

/// @brief Pre-pass: Eagerly resolve types of final constants from literal initializers.
/// @details Scans declarations for final globals with literal initializers and updates
///          the registered symbol type from unknown() to the concrete literal type.
///          This allows forward references to final constants in entity/function bodies.
/// @param declarations The declaration list to process.
void Sema::registerFinalConstantTypes(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        if (decl->kind == DeclKind::GlobalVar)
        {
            auto *gvar = static_cast<GlobalVarDecl *>(decl.get());
            if (!gvar->isFinal || !gvar->initializer)
                continue;

            // Look up the symbol — it was registered in Pass 1 with unknown type
            std::string name = qualifyName(gvar->name);
            Symbol *sym = lookupSymbol(name);
            if (!sym || !sym->type->isUnknown())
                continue;

            // Infer type directly from literal initializer
            Expr *init = gvar->initializer.get();
            TypeRef inferredType = nullptr;
            if (dynamic_cast<IntLiteralExpr *>(init))
                inferredType = types::integer();
            else if (dynamic_cast<NumberLiteralExpr *>(init))
                inferredType = types::number();
            else if (dynamic_cast<BoolLiteralExpr *>(init))
                inferredType = types::boolean();
            else if (dynamic_cast<StringLiteralExpr *>(init))
                inferredType = types::string();
            else if (auto *unary = dynamic_cast<UnaryExpr *>(init))
            {
                // Handle negated literals: final X = -42
                if (unary->op == UnaryOp::Neg)
                {
                    if (dynamic_cast<IntLiteralExpr *>(unary->operand.get()))
                        inferredType = types::integer();
                    else if (dynamic_cast<NumberLiteralExpr *>(unary->operand.get()))
                        inferredType = types::number();
                }
            }

            if (inferredType)
                sym->type = inferredType;
        }
        else if (decl->kind == DeclKind::Namespace)
        {
            // Recurse into namespace declarations
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerFinalConstantTypes(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

/// @brief Pass 3: Analyze declaration bodies (functions, types, globals).
/// @param declarations The declaration list to process.
void Sema::analyzeDeclarationBodies(std::vector<DeclPtr> &declarations)
{
    for (auto &decl : declarations)
    {
        switch (decl->kind)
        {
            case DeclKind::Function:
                analyzeFunctionDecl(*static_cast<FunctionDecl *>(decl.get()));
                break;
            case DeclKind::Value:
                analyzeValueDecl(*static_cast<ValueDecl *>(decl.get()));
                break;
            case DeclKind::Entity:
                analyzeEntityDecl(*static_cast<EntityDecl *>(decl.get()));
                break;
            case DeclKind::Interface:
                analyzeInterfaceDecl(*static_cast<InterfaceDecl *>(decl.get()));
                break;
            case DeclKind::GlobalVar:
                analyzeGlobalVarDecl(*static_cast<GlobalVarDecl *>(decl.get()));
                break;
            default:
                break;
        }
    }
}

/// @brief Analyze a namespace declaration with recursive multi-pass processing.
/// @details Saves the current namespace prefix, computes a new qualified prefix,
///          then runs the same three-pass strategy (register, member sigs, bodies)
///          on the namespace's nested declarations. Handles nested namespaces recursively.
/// @param decl The namespace declaration to analyze.
void Sema::analyzeNamespaceDecl(NamespaceDecl &decl)
{
    // Save current namespace prefix
    std::string savedPrefix = namespacePrefix_;

    // Compute new prefix: append this namespace's name
    if (namespacePrefix_.empty())
        namespacePrefix_ = decl.name;
    else
        namespacePrefix_ = namespacePrefix_ + "." + decl.name;

    // Process declarations inside this namespace
    // First pass: register declarations
    for (auto &innerDecl : decl.declarations)
    {
        switch (innerDecl->kind)
        {
            case DeclKind::Function:
            {
                auto *func = static_cast<FunctionDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(func->name);

                TypeRef returnType =
                    func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();

                std::vector<TypeRef> paramTypes;
                for (const auto &param : func->params)
                {
                    paramTypes.push_back(param.type ? resolveTypeNode(param.type.get())
                                                    : types::unknown());
                }
                auto funcType = types::function(paramTypes, returnType);

                Symbol sym;
                sym.kind = Symbol::Kind::Function;
                sym.name = qualifiedName;
                sym.type = funcType;
                sym.decl = func;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::Value:
            {
                auto *value = static_cast<ValueDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(value->name);
                valueDecls_[qualifiedName] = value;
                auto valueType = types::value(qualifiedName);
                typeRegistry_[qualifiedName] = valueType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = valueType;
                sym.decl = value;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::Entity:
            {
                auto *entity = static_cast<EntityDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(entity->name);
                entityDecls_[qualifiedName] = entity;
                auto entityType = types::entity(qualifiedName);
                typeRegistry_[qualifiedName] = entityType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = entityType;
                sym.decl = entity;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::Interface:
            {
                auto *iface = static_cast<InterfaceDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(iface->name);
                interfaceDecls_[qualifiedName] = iface;
                auto ifaceType = types::interface(qualifiedName);
                typeRegistry_[qualifiedName] = ifaceType;

                Symbol sym;
                sym.kind = Symbol::Kind::Type;
                sym.name = qualifiedName;
                sym.type = ifaceType;
                sym.decl = iface;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::GlobalVar:
            {
                auto *gvar = static_cast<GlobalVarDecl *>(innerDecl.get());
                std::string qualifiedName = qualifyName(gvar->name);

                TypeRef varType;
                if (gvar->type)
                    varType = resolveTypeNode(gvar->type.get());
                else
                    varType = types::unknown();

                Symbol sym;
                sym.kind = Symbol::Kind::Variable;
                sym.name = qualifiedName;
                sym.type = varType;
                sym.isFinal = gvar->isFinal;
                sym.decl = gvar;
                defineSymbol(qualifiedName, sym);
                break;
            }
            case DeclKind::Namespace:
            {
                // Nested namespace - recursively process
                analyzeNamespaceDecl(*static_cast<NamespaceDecl *>(innerDecl.get()));
                break;
            }
            default:
                break;
        }
    }

    // Pre-pass: resolve final constant types for forward references
    registerFinalConstantTypes(decl.declarations);

    // Second pass: register members for types
    registerMemberSignatures(decl.declarations);

    // Third pass: analyze bodies
    analyzeDeclarationBodies(decl.declarations);

    // Restore previous namespace prefix
    namespacePrefix_ = savedPrefix;
}

// initRuntimeFunctions() implementation moved to Sema_Runtime.cpp
} // namespace il::frontends::zia
