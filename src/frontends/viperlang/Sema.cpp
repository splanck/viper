//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema.cpp
/// @brief Implementation of ViperLang semantic analyzer.
///
/// @details This file implements the Sema class which performs type checking
/// and name resolution on ViperLang ASTs. Key implementation details:
///
/// ## Two-Pass Analysis
///
/// 1. **First pass**: Register all top-level declarations (functions, types,
///    global variables) in the global scope without analyzing bodies
/// 2. **Second pass**: Analyze declaration bodies with full symbol visibility
///
/// ## Scope Management
///
/// Scopes are dynamically allocated and linked via parent pointers:
/// - Global scope: Functions, types, global variables
/// - Type scope: Fields and methods of value/entity/interface
/// - Function scope: Parameters
/// - Block scope: Local variables
///
/// ## Expression Type Inference
///
/// Expression types are computed bottom-up and cached in exprTypes_ map.
/// Each analyzeXxx method returns the inferred type and stores it.
///
/// ## Function Resolution
///
/// Calls to functions with dotted names (Viper.Terminal.Say, MyLib.helper, etc.)
/// are detected by extracting the qualified name from field access chains and
/// looking them up in the symbol table. Both runtime (extern) functions and
/// user-defined namespaced functions use the same unified lookup mechanism.
/// Resolved extern calls are stored in runtimeCallees_ for the lowerer.
///
/// @see Sema.hpp for the class interface
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"
#include <cassert>
#include <functional>
#include <set>
#include <sstream>

namespace il::frontends::viperlang
{

//=============================================================================
// Scope Implementation
//=============================================================================

void Scope::define(const std::string &name, Symbol symbol)
{
    symbols_[name] = std::move(symbol);
}

Symbol *Scope::lookup(const std::string &name)
{
    auto it = symbols_.find(name);
    if (it != symbols_.end())
        return &it->second;
    if (parent_)
        return parent_->lookup(name);
    return nullptr;
}

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

bool Sema::analyze(ModuleDecl &module)
{
    currentModule_ = &module;

    for (auto &import : module.imports)
    {
        analyzeImport(import);
    }

    // First pass: register all top-level declarations
    for (auto &decl : module.declarations)
    {
        switch (decl->kind)
        {
            case DeclKind::Function:
            {
                auto *func = static_cast<FunctionDecl *>(decl.get());
                // Determine return type
                TypeRef returnType =
                    func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();

                // Build function type
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
                break;
            }
            case DeclKind::Value:
            {
                auto *value = static_cast<ValueDecl *>(decl.get());
                valueDecls_[value->name] = value;
                auto valueType = types::value(value->name);
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
                auto entityType = types::entity(entity->name);
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

    // Second pass: register all method/field signatures (before analyzing bodies)
    // This ensures cross-module method calls can be resolved regardless of declaration order
    for (auto &decl : module.declarations)
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

    // Third pass: analyze declarations (bodies)
    for (auto &decl : module.declarations)
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

    return !hasError_;
}

TypeRef Sema::typeOf(const Expr *expr) const
{
    auto it = exprTypes_.find(expr);
    return it != exprTypes_.end() ? it->second : types::unknown();
}

TypeRef Sema::resolveType(const TypeNode *node) const
{
    return const_cast<Sema *>(this)->resolveTypeNode(node);
}

//=============================================================================
// Type Resolution
//=============================================================================

TypeRef Sema::resolveNamedType(const std::string &name) const
{
    // Built-in types (accept both PascalCase and lowercase variants)
    if (name == "Integer" || name == "integer" || name == "Int" || name == "int")
        return types::integer();
    if (name == "Number" || name == "number" || name == "Float" || name == "float" ||
        name == "Double" || name == "double")
        return types::number();
    if (name == "Boolean" || name == "boolean" || name == "Bool" || name == "bool")
        return types::boolean();
    if (name == "String" || name == "string")
        return types::string();
    if (name == "Byte" || name == "byte")
        return types::byte();
    if (name == "Unit" || name == "unit")
        return types::unit();
    if (name == "Void" || name == "void")
        return types::voidType();
    if (name == "Error" || name == "error")
        return types::error();
    if (name == "Ptr" || name == "ptr")
        return types::ptr();

    // Look up in registry
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end())
        return it->second;

    // Handle cross-module type references (e.g., "token.Token")
    // The ImportResolver merges imported declarations, so we just need
    // to strip the module prefix and look up the base type name.
    auto dotPos = name.find('.');
    if (dotPos != std::string::npos)
    {
        std::string typeName = name.substr(dotPos + 1);
        // Look up the unqualified type name in the registry
        it = typeRegistry_.find(typeName);
        if (it != typeRegistry_.end())
            return it->second;
    }

    return nullptr;
}

TypeRef Sema::resolveTypeNode(const TypeNode *node)
{
    if (!node)
        return types::unknown();

    switch (node->kind)
    {
        case TypeKind::Named:
        {
            auto *named = static_cast<const NamedType *>(node);
            TypeRef resolved = resolveNamedType(named->name);
            if (!resolved)
            {
                error(node->loc, "Unknown type: " + named->name);
                return types::unknown();
            }
            return resolved;
        }

        case TypeKind::Generic:
        {
            auto *generic = static_cast<const GenericType *>(node);
            std::vector<TypeRef> args;
            for (const auto &arg : generic->args)
            {
                args.push_back(resolveTypeNode(arg.get()));
            }

            // Built-in generic types
            if (generic->name == "List")
            {
                return types::list(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Set")
            {
                return types::set(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Map")
            {
                TypeRef keyType = args.size() > 0 ? args[0] : types::unknown();
                TypeRef valueType = args.size() > 1 ? args[1] : types::unknown();
                if (keyType && keyType->kind != TypeKindSem::Unknown &&
                    keyType->kind != TypeKindSem::String)
                {
                    error(node->loc, "Map keys must be String");
                }
                return types::map(keyType, valueType);
            }
            if (generic->name == "Result")
            {
                return types::result(args.empty() ? types::unit() : args[0]);
            }

            // User-defined generic type
            TypeRef baseType = resolveNamedType(generic->name);
            if (!baseType)
            {
                error(node->loc, "Unknown type: " + generic->name);
                return types::unknown();
            }

            // Create instantiated type
            return std::make_shared<ViperType>(baseType->kind, baseType->name, args);
        }

        case TypeKind::Optional:
        {
            auto *opt = static_cast<const OptionalType *>(node);
            TypeRef inner = resolveTypeNode(opt->inner.get());
            return types::optional(inner);
        }

        case TypeKind::Function:
        {
            auto *func = static_cast<const FunctionType *>(node);
            std::vector<TypeRef> params;
            for (const auto &param : func->params)
            {
                params.push_back(resolveTypeNode(param.get()));
            }
            TypeRef ret =
                func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();
            return types::function(params, ret);
        }

        case TypeKind::Tuple:
        {
            const auto *tupleType = static_cast<const TupleType *>(node);
            std::vector<TypeRef> elementTypes;
            for (const auto &elem : tupleType->elements)
            {
                elementTypes.push_back(resolveType(elem.get()));
            }
            return types::tuple(std::move(elementTypes));
        }
    }

    return types::unknown();
}

//=============================================================================
// Scope Management
//=============================================================================

void Sema::pushScope()
{
    scopes_.push_back(std::make_unique<Scope>(currentScope_));
    currentScope_ = scopes_.back().get();
}

void Sema::popScope()
{
    assert(scopes_.size() > 1 && "cannot pop global scope");
    currentScope_ = currentScope_->parent();
    scopes_.pop_back();
    assert(currentScope_ == scopes_.back().get() && "scope stack corrupted");
}

void Sema::defineSymbol(const std::string &name, Symbol symbol)
{
    currentScope_->define(name, std::move(symbol));
}

void Sema::defineExternFunction(const std::string &name, TypeRef returnType)
{
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = name;
    sym.type = returnType; // For extern functions, we store just the return type
    sym.isExtern = true;
    sym.decl = nullptr; // No AST declaration for extern functions
    defineSymbol(name, std::move(sym));
}

Symbol *Sema::lookupSymbol(const std::string &name)
{
    return currentScope_->lookup(name);
}

TypeRef Sema::lookupVarType(const std::string &name)
{
    Symbol *sym = currentScope_->lookup(name);
    if (sym && (sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Parameter))
    {
        return sym->type;
    }
    return nullptr;
}

//=============================================================================
// Closure Capture Collection
//=============================================================================

void Sema::collectCaptures(const Expr *expr,
                           const std::set<std::string> &lambdaLocals,
                           std::vector<CapturedVar> &captures)
{
    if (!expr)
        return;

    std::set<std::string> captured;

    // Helper to recursively collect identifiers
    std::function<void(const Expr *)> collect = [&](const Expr *e)
    {
        if (!e)
            return;

        switch (e->kind)
        {
            case ExprKind::Ident:
            {
                auto *ident = static_cast<const IdentExpr *>(e);
                // Check if this is a local variable (not a lambda param, not a function)
                if (lambdaLocals.find(ident->name) == lambdaLocals.end())
                {
                    Symbol *sym = lookupSymbol(ident->name);
                    if (sym && (sym->kind == Symbol::Kind::Variable ||
                                sym->kind == Symbol::Kind::Parameter))
                    {
                        if (captured.find(ident->name) == captured.end())
                        {
                            captured.insert(ident->name);
                            CapturedVar cv;
                            cv.name = ident->name;
                            cv.byReference = !sym->isFinal; // Mutable vars by reference
                            captures.push_back(cv);
                        }
                    }
                }
                break;
            }
            case ExprKind::Binary:
            {
                auto *bin = static_cast<const BinaryExpr *>(e);
                collect(bin->left.get());
                collect(bin->right.get());
                break;
            }
            case ExprKind::Unary:
            {
                auto *unary = static_cast<const UnaryExpr *>(e);
                collect(unary->operand.get());
                break;
            }
            case ExprKind::Call:
            {
                auto *call = static_cast<const CallExpr *>(e);
                collect(call->callee.get());
                for (const auto &arg : call->args)
                    collect(arg.value.get());
                break;
            }
            case ExprKind::Field:
            {
                auto *field = static_cast<const FieldExpr *>(e);
                collect(field->base.get());
                break;
            }
            case ExprKind::Index:
            {
                auto *idx = static_cast<const IndexExpr *>(e);
                collect(idx->base.get());
                collect(idx->index.get());
                break;
            }
            case ExprKind::Block:
            {
                auto *block = static_cast<const BlockExpr *>(e);
                // Would need to handle statements - skip for now
                break;
            }
            case ExprKind::If:
            {
                auto *ifExpr = static_cast<const IfExpr *>(e);
                collect(ifExpr->condition.get());
                collect(ifExpr->thenBranch.get());
                if (ifExpr->elseBranch)
                    collect(ifExpr->elseBranch.get());
                break;
            }
            case ExprKind::Match:
            {
                auto *match = static_cast<const MatchExpr *>(e);
                collect(match->scrutinee.get());
                for (const auto &arm : match->arms)
                    collect(arm.body.get());
                break;
            }
            case ExprKind::Tuple:
            {
                auto *tuple = static_cast<const TupleExpr *>(e);
                for (const auto &elem : tuple->elements)
                    collect(elem.get());
                break;
            }
            case ExprKind::TupleIndex:
            {
                auto *ti = static_cast<const TupleIndexExpr *>(e);
                collect(ti->tuple.get());
                break;
            }
            case ExprKind::ListLiteral:
            {
                auto *list = static_cast<const ListLiteralExpr *>(e);
                for (const auto &elem : list->elements)
                    collect(elem.get());
                break;
            }
            case ExprKind::Lambda:
            {
                // Nested lambda - don't descend, it will handle its own captures
                break;
            }
            default:
                // Literals and other expressions don't reference variables
                break;
        }
    };

    collect(expr);
}

//=============================================================================
// Error Reporting
//=============================================================================

void Sema::error(SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, "V3000"});
}

void Sema::errorUndefined(SourceLoc loc, const std::string &name)
{
    error(loc, "Undefined identifier: " + name);
}

void Sema::errorTypeMismatch(SourceLoc loc, TypeRef expected, TypeRef actual)
{
    error(loc, "Type mismatch: expected " + expected->toString() + ", got " + actual->toString());
}

//=============================================================================
// Built-in Functions
//=============================================================================

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

std::string Sema::qualifyName(const std::string &name) const
{
    if (namespacePrefix_.empty())
        return name;
    return namespacePrefix_ + "." + name;
}

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

    // Second pass: register members for types
    for (auto &innerDecl : decl.declarations)
    {
        switch (innerDecl->kind)
        {
            case DeclKind::Value:
                registerValueMembers(*static_cast<ValueDecl *>(innerDecl.get()));
                break;
            case DeclKind::Entity:
                registerEntityMembers(*static_cast<EntityDecl *>(innerDecl.get()));
                break;
            case DeclKind::Interface:
                registerInterfaceMembers(*static_cast<InterfaceDecl *>(innerDecl.get()));
                break;
            default:
                break;
        }
    }

    // Third pass: analyze bodies
    for (auto &innerDecl : decl.declarations)
    {
        switch (innerDecl->kind)
        {
            case DeclKind::Function:
                analyzeFunctionDecl(*static_cast<FunctionDecl *>(innerDecl.get()));
                break;
            case DeclKind::Value:
                analyzeValueDecl(*static_cast<ValueDecl *>(innerDecl.get()));
                break;
            case DeclKind::Entity:
                analyzeEntityDecl(*static_cast<EntityDecl *>(innerDecl.get()));
                break;
            case DeclKind::Interface:
                analyzeInterfaceDecl(*static_cast<InterfaceDecl *>(innerDecl.get()));
                break;
            case DeclKind::GlobalVar:
                analyzeGlobalVarDecl(*static_cast<GlobalVarDecl *>(innerDecl.get()));
                break;
            default:
                break;
        }
    }

    // Restore previous namespace prefix
    namespacePrefix_ = savedPrefix;
}

// initRuntimeFunctions() implementation moved to Sema_Runtime.cpp
} // namespace il::frontends::viperlang
