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
/// ## Runtime Function Resolution
///
/// Calls to runtime functions (Viper.Terminal.Say, etc.) are detected by
/// extracting dotted names from field access chains and looking them up
/// in runtimeFunctions_. Resolved calls are stored in runtimeCallees_.
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
            default:
                break;
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

void Sema::initRuntimeFunctions()
{
    // Auto-generated from src/il/runtime/runtime.def
    // This registers all 1002 Viper.* namespace functions with their return types

    // =========================================================================
    // BITS
    // =========================================================================
    runtimeFunctions_["Viper.Bits.And"] = types::integer();
    runtimeFunctions_["Viper.Bits.Clear"] = types::integer();
    runtimeFunctions_["Viper.Bits.Count"] = types::integer();
    runtimeFunctions_["Viper.Bits.Flip"] = types::integer();
    runtimeFunctions_["Viper.Bits.Get"] = types::boolean();
    runtimeFunctions_["Viper.Bits.LeadZ"] = types::integer();
    runtimeFunctions_["Viper.Bits.Not"] = types::integer();
    runtimeFunctions_["Viper.Bits.Or"] = types::integer();
    runtimeFunctions_["Viper.Bits.Rotl"] = types::integer();
    runtimeFunctions_["Viper.Bits.Rotr"] = types::integer();
    runtimeFunctions_["Viper.Bits.Set"] = types::integer();
    runtimeFunctions_["Viper.Bits.Shl"] = types::integer();
    runtimeFunctions_["Viper.Bits.Shr"] = types::integer();
    runtimeFunctions_["Viper.Bits.Swap"] = types::integer();
    runtimeFunctions_["Viper.Bits.Toggle"] = types::integer();
    runtimeFunctions_["Viper.Bits.TrailZ"] = types::integer();
    runtimeFunctions_["Viper.Bits.Ushr"] = types::integer();
    runtimeFunctions_["Viper.Bits.Xor"] = types::integer();

    // =========================================================================
    // BOX
    // =========================================================================
    runtimeFunctions_["Viper.Box.I64"] = types::ptr();
    runtimeFunctions_["Viper.Box.F64"] = types::ptr();
    runtimeFunctions_["Viper.Box.I1"] = types::ptr();
    runtimeFunctions_["Viper.Box.Str"] = types::ptr();
    runtimeFunctions_["Viper.Box.ToI64"] = types::integer();
    runtimeFunctions_["Viper.Box.ToF64"] = types::number();
    runtimeFunctions_["Viper.Box.ToI1"] = types::integer();
    runtimeFunctions_["Viper.Box.ToStr"] = types::string();
    runtimeFunctions_["Viper.Box.Type"] = types::integer();
    runtimeFunctions_["Viper.Box.EqI64"] = types::integer();
    runtimeFunctions_["Viper.Box.EqF64"] = types::integer();
    runtimeFunctions_["Viper.Box.EqStr"] = types::integer();

    // =========================================================================
    // COLLECTIONS - BYTES
    // =========================================================================
    runtimeFunctions_["Viper.Collections.Bytes.Clone"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.Copy"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Bytes.Fill"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Bytes.Find"] = types::integer();
    runtimeFunctions_["Viper.Collections.Bytes.FromBase64"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.FromHex"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.FromStr"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.Get"] = types::integer();
    runtimeFunctions_["Viper.Collections.Bytes.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Bytes.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.Set"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Bytes.Slice"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bytes.ToBase64"] = types::string();
    runtimeFunctions_["Viper.Collections.Bytes.ToHex"] = types::string();
    runtimeFunctions_["Viper.Collections.Bytes.ToStr"] = types::string();

    // =========================================================================
    // COLLECTIONS - BAG
    // =========================================================================
    runtimeFunctions_["Viper.Collections.Bag.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Bag.Common"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bag.Diff"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bag.Drop"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Bag.Has"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Bag.get_IsEmpty"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Bag.Items"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bag.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Bag.Merge"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bag.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Bag.Put"] = types::boolean();

    // =========================================================================
    // COLLECTIONS - LIST
    // =========================================================================
    runtimeFunctions_["Viper.Collections.List.Add"] = types::voidType();
    runtimeFunctions_["Viper.Collections.List.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.List.Find"] = types::integer();
    runtimeFunctions_["Viper.Collections.List.get_Count"] = types::integer();
    runtimeFunctions_["Viper.Collections.List.get_Item"] = types::ptr();
    runtimeFunctions_["Viper.Collections.List.Has"] = types::boolean();
    runtimeFunctions_["Viper.Collections.List.Insert"] = types::voidType();
    runtimeFunctions_["Viper.Collections.List.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.List.Remove"] = types::boolean();
    runtimeFunctions_["Viper.Collections.List.RemoveAt"] = types::voidType();
    runtimeFunctions_["Viper.Collections.List.set_Item"] = types::voidType();

    // =========================================================================
    // COLLECTIONS - MAP
    // =========================================================================
    runtimeFunctions_["Viper.Collections.Map.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Map.Get"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Map.GetOr"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Map.Has"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Map.get_IsEmpty"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Map.Keys"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Map.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Map.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Map.Remove"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Map.Set"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Map.SetIfMissing"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Map.Values"] = types::ptr();

    // =========================================================================
    // COLLECTIONS - QUEUE, STACK, HEAP, SEQ, RING, TREEMAP
    // =========================================================================
    runtimeFunctions_["Viper.Collections.Queue.Add"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Queue.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Queue.get_IsEmpty"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Queue.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Queue.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Queue.Peek"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Queue.Take"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Heap.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Heap.get_IsEmpty"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Heap.get_IsMax"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Heap.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Heap.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Heap.NewMax"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Heap.Peek"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Heap.Pop"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Heap.Push"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Heap.ToSeq"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Stack.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Stack.get_IsEmpty"] = types::boolean();
    runtimeFunctions_["Viper.Collections.Stack.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Stack.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Stack.Peek"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Stack.Pop"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Stack.Push"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Seq.Push"] = types::voidType(); // VL-012: was Add
    runtimeFunctions_["Viper.Collections.Seq.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Seq.Get"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Seq.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Seq.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Seq.Pop"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Seq.Set"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Ring.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.Ring.Get"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Ring.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.Ring.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Ring.Pop"] = types::ptr();
    runtimeFunctions_["Viper.Collections.Ring.Push"] = types::voidType();
    runtimeFunctions_["Viper.Collections.TreeMap.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Collections.TreeMap.Get"] = types::ptr();
    runtimeFunctions_["Viper.Collections.TreeMap.Has"] = types::boolean();
    runtimeFunctions_["Viper.Collections.TreeMap.Keys"] = types::ptr();
    runtimeFunctions_["Viper.Collections.TreeMap.get_Len"] = types::integer();
    runtimeFunctions_["Viper.Collections.TreeMap.New"] = types::ptr();
    runtimeFunctions_["Viper.Collections.TreeMap.Drop"] = types::boolean(); // VL-013: was Remove
    runtimeFunctions_["Viper.Collections.TreeMap.Set"] = types::voidType();

    // =========================================================================
    // CRYPTO - HASH, RAND, KEYDERIVE
    // =========================================================================
    runtimeFunctions_["Viper.Crypto.Hash.CRC32"] = types::integer();
    runtimeFunctions_["Viper.Crypto.Hash.MD5"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.SHA1"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.SHA256"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.SHA384"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.SHA512"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.HmacMD5"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.HmacSHA1"] = types::string();
    runtimeFunctions_["Viper.Crypto.Hash.HmacSHA256"] = types::string();
    runtimeFunctions_["Viper.Crypto.Rand.Bytes"] = types::ptr();
    runtimeFunctions_["Viper.Crypto.Rand.Int"] = types::integer();
    runtimeFunctions_["Viper.Crypto.KeyDerive.Pbkdf2SHA256"] = types::ptr();

    // =========================================================================
    // DATETIME
    // =========================================================================
    runtimeFunctions_["Viper.DateTime.Now"] = types::integer();
    runtimeFunctions_["Viper.DateTime.UtcNow"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Create"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Format"] = types::string();
    runtimeFunctions_["Viper.DateTime.Parse"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Year"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Month"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Day"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Hour"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Minute"] = types::integer();
    runtimeFunctions_["Viper.DateTime.Second"] = types::integer();
    runtimeFunctions_["Viper.DateTime.DayOfWeek"] = types::integer();
    runtimeFunctions_["Viper.DateTime.DayOfYear"] = types::integer();
    runtimeFunctions_["Viper.DateTime.AddDays"] = types::integer();
    runtimeFunctions_["Viper.DateTime.AddHours"] = types::integer();
    runtimeFunctions_["Viper.DateTime.AddMinutes"] = types::integer();
    runtimeFunctions_["Viper.DateTime.AddSeconds"] = types::integer();
    runtimeFunctions_["Viper.DateTime.ToUnix"] = types::integer();
    runtimeFunctions_["Viper.DateTime.FromUnix"] = types::integer();

    // =========================================================================
    // DIAGNOSTICS
    // =========================================================================
    runtimeFunctions_["Viper.Diagnostics.Assert"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.AssertEq"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.AssertNe"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.AssertTrue"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.AssertFalse"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.Trap"] = types::voidType();
    runtimeFunctions_["Viper.Diagnostics.Stopwatch.Start"] = types::ptr();
    runtimeFunctions_["Viper.Diagnostics.Stopwatch.Elapsed"] = types::integer();
    runtimeFunctions_["Viper.Diagnostics.Stopwatch.ElapsedMs"] = types::integer();
    runtimeFunctions_["Viper.Diagnostics.Stopwatch.Reset"] = types::voidType();

    // =========================================================================
    // ENVIRONMENT
    // =========================================================================
    runtimeFunctions_["Viper.Environment.GetArgument"] = types::string();
    runtimeFunctions_["Viper.Environment.GetArgumentCount"] = types::integer();
    runtimeFunctions_["Viper.Environment.GetCommandLine"] = types::string();
    runtimeFunctions_["Viper.Environment.GetVar"] = types::string();
    runtimeFunctions_["Viper.Environment.SetVar"] = types::voidType();
    runtimeFunctions_["Viper.Environment.HasVar"] = types::boolean();

    // =========================================================================
    // EXEC
    // =========================================================================
    runtimeFunctions_["Viper.Exec.Run"] = types::integer();
    runtimeFunctions_["Viper.Exec.Capture"] = types::string();
    runtimeFunctions_["Viper.Exec.Shell"] = types::integer();

    // =========================================================================
    // FMT (FORMATTING)
    // =========================================================================
    runtimeFunctions_["Viper.Fmt.Str"] = types::string();
    runtimeFunctions_["Viper.Fmt.Int"] = types::string();
    runtimeFunctions_["Viper.Fmt.Num"] = types::string();
    runtimeFunctions_["Viper.Fmt.Bool"] = types::string();
    runtimeFunctions_["Viper.Fmt.Pad"] = types::string();
    runtimeFunctions_["Viper.Fmt.PadLeft"] = types::string();
    runtimeFunctions_["Viper.Fmt.PadRight"] = types::string();
    runtimeFunctions_["Viper.Fmt.Hex"] = types::string();
    runtimeFunctions_["Viper.Fmt.Oct"] = types::string();
    runtimeFunctions_["Viper.Fmt.Bin"] = types::string();
    runtimeFunctions_["Viper.Fmt.Size"] = types::string();

    // =========================================================================
    // GRAPHICS
    // =========================================================================
    runtimeFunctions_["Viper.Graphics.Canvas.New"] = types::ptr();
    runtimeFunctions_["Viper.Graphics.Canvas.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.Plot"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.Line"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.Box"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.BoxFill"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.Circle"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.CircleFill"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.Text"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Canvas.GetPixel"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Canvas.get_Width"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Canvas.get_Height"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.RGB"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.RGBA"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.Red"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.Green"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.Blue"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.Alpha"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Color.Blend"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Pixels.New"] = types::ptr();
    runtimeFunctions_["Viper.Graphics.Pixels.Load"] = types::ptr();
    runtimeFunctions_["Viper.Graphics.Pixels.Get"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Pixels.Set"] = types::voidType();
    runtimeFunctions_["Viper.Graphics.Pixels.get_Width"] = types::integer();
    runtimeFunctions_["Viper.Graphics.Pixels.get_Height"] = types::integer();

    // =========================================================================
    // INPUT
    // =========================================================================
    runtimeFunctions_["Viper.Input.Keyboard.IsDown"] = types::boolean();
    runtimeFunctions_["Viper.Input.Keyboard.WasPressed"] = types::boolean();
    runtimeFunctions_["Viper.Input.Mouse.GetX"] = types::integer();
    runtimeFunctions_["Viper.Input.Mouse.GetY"] = types::integer();
    runtimeFunctions_["Viper.Input.Mouse.IsDown"] = types::boolean();
    runtimeFunctions_["Viper.Input.Pad.IsConnected"] = types::boolean();
    runtimeFunctions_["Viper.Input.Pad.GetAxis"] = types::number();
    runtimeFunctions_["Viper.Input.Pad.IsDown"] = types::boolean();

    // =========================================================================
    // IO - FILE (VL-017 fixes: align names and return types with runtime)
    // =========================================================================
    runtimeFunctions_["Viper.IO.File.Exists"] = types::boolean();
    runtimeFunctions_["Viper.IO.File.Size"] = types::integer();
    runtimeFunctions_["Viper.IO.File.Delete"] = types::voidType(); // VL-017: was boolean
    runtimeFunctions_["Viper.IO.File.Copy"] = types::voidType();   // VL-017: was boolean
    runtimeFunctions_["Viper.IO.File.Move"] = types::voidType();   // VL-017: was boolean
    runtimeFunctions_["Viper.IO.File.ReadAllText"] = types::string();
    runtimeFunctions_["Viper.IO.File.WriteAllText"] = types::voidType();
    runtimeFunctions_["Viper.IO.File.ReadAllBytes"] = types::ptr();
    runtimeFunctions_["Viper.IO.File.WriteAllBytes"] = types::voidType();
    runtimeFunctions_["Viper.IO.File.Append"] = types::voidType();  // VL-017: was AppendText
    runtimeFunctions_["Viper.IO.File.Modified"] = types::integer(); // VL-017: was GetModTime
    runtimeFunctions_["Viper.IO.File.Touch"] = types::voidType();

    // =========================================================================
    // IO - DIR (VL-016 fixes: align names with runtime)
    // =========================================================================
    runtimeFunctions_["Viper.IO.Dir.Make"] = types::voidType(); // VL-016: was Create
    runtimeFunctions_["Viper.IO.Dir.MakeAll"] = types::voidType();
    runtimeFunctions_["Viper.IO.Dir.Remove"] = types::voidType(); // VL-016: was Delete
    runtimeFunctions_["Viper.IO.Dir.RemoveAll"] = types::voidType();
    runtimeFunctions_["Viper.IO.Dir.Exists"] = types::boolean();
    runtimeFunctions_["Viper.IO.Dir.List"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.ListSeq"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.Files"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.FilesSeq"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.Dirs"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.DirsSeq"] = types::ptr();
    runtimeFunctions_["Viper.IO.Dir.Current"] = types::string(); // VL-016: was GetCurrent
    runtimeFunctions_["Viper.IO.Dir.SetCurrent"] = types::voidType();

    // =========================================================================
    // IO - PATH (VL-014, VL-015 fixes: align names with runtime)
    // =========================================================================
    runtimeFunctions_["Viper.IO.Path.Join"] = types::string();
    runtimeFunctions_["Viper.IO.Path.Dir"] = types::string();  // VL-014: was GetDir
    runtimeFunctions_["Viper.IO.Path.Name"] = types::string(); // VL-014: was GetName
    runtimeFunctions_["Viper.IO.Path.Ext"] = types::string();  // VL-014: was GetExt
    runtimeFunctions_["Viper.IO.Path.Stem"] = types::string(); // VL-014: was GetBase
    runtimeFunctions_["Viper.IO.Path.Norm"] = types::string(); // VL-014: was Normalize
    runtimeFunctions_["Viper.IO.Path.Abs"] = types::string();  // VL-014: was Absolute
    runtimeFunctions_["Viper.IO.Path.IsAbs"] = types::boolean();
    runtimeFunctions_["Viper.IO.Path.Sep"] = types::string();
    runtimeFunctions_["Viper.IO.Path.WithExt"] = types::string();

    // =========================================================================
    // IO - BINFILE, LINEREADER, LINEWRITER
    // =========================================================================
    runtimeFunctions_["Viper.IO.BinFile.Open"] = types::ptr();
    runtimeFunctions_["Viper.IO.BinFile.Close"] = types::voidType();
    runtimeFunctions_["Viper.IO.BinFile.Read"] = types::ptr();
    runtimeFunctions_["Viper.IO.BinFile.Write"] = types::integer();
    runtimeFunctions_["Viper.IO.BinFile.Seek"] = types::integer();
    runtimeFunctions_["Viper.IO.BinFile.Tell"] = types::integer();
    runtimeFunctions_["Viper.IO.BinFile.Eof"] = types::boolean();
    runtimeFunctions_["Viper.IO.LineReader.Open"] = types::ptr();
    runtimeFunctions_["Viper.IO.LineReader.ReadLine"] = types::string();
    runtimeFunctions_["Viper.IO.LineReader.Close"] = types::voidType();
    runtimeFunctions_["Viper.IO.LineReader.Eof"] = types::boolean();
    runtimeFunctions_["Viper.IO.LineWriter.Open"] = types::ptr();
    runtimeFunctions_["Viper.IO.LineWriter.WriteLine"] = types::voidType();
    runtimeFunctions_["Viper.IO.LineWriter.Close"] = types::voidType();

    // =========================================================================
    // IO - COMPRESS, ARCHIVE, MEMSTREAM, WATCHER
    // =========================================================================
    runtimeFunctions_["Viper.IO.Compress.Deflate"] = types::ptr();
    runtimeFunctions_["Viper.IO.Compress.Inflate"] = types::ptr();
    runtimeFunctions_["Viper.IO.Compress.GzipCompress"] = types::ptr();
    runtimeFunctions_["Viper.IO.Compress.GzipDecompress"] = types::ptr();
    runtimeFunctions_["Viper.IO.Archive.New"] = types::ptr();
    runtimeFunctions_["Viper.IO.Archive.Open"] = types::ptr();
    runtimeFunctions_["Viper.IO.Archive.AddFile"] = types::voidType();
    runtimeFunctions_["Viper.IO.Archive.AddBytes"] = types::voidType();
    runtimeFunctions_["Viper.IO.Archive.ExtractAll"] = types::voidType();
    runtimeFunctions_["Viper.IO.Archive.List"] = types::ptr();
    runtimeFunctions_["Viper.IO.Archive.Close"] = types::voidType();
    runtimeFunctions_["Viper.IO.MemStream.New"] = types::ptr();
    runtimeFunctions_["Viper.IO.MemStream.Read"] = types::ptr();
    runtimeFunctions_["Viper.IO.MemStream.Write"] = types::integer();
    runtimeFunctions_["Viper.IO.MemStream.Seek"] = types::integer();
    runtimeFunctions_["Viper.IO.MemStream.ToBytes"] = types::ptr();
    runtimeFunctions_["Viper.IO.Watcher.New"] = types::ptr();
    runtimeFunctions_["Viper.IO.Watcher.Next"] = types::ptr();
    runtimeFunctions_["Viper.IO.Watcher.Close"] = types::voidType();

    // =========================================================================
    // LOG
    // =========================================================================
    runtimeFunctions_["Viper.Log.Info"] = types::voidType();
    runtimeFunctions_["Viper.Log.Warn"] = types::voidType();
    runtimeFunctions_["Viper.Log.Error"] = types::voidType();
    runtimeFunctions_["Viper.Log.Debug"] = types::voidType();

    // =========================================================================
    // MACHINE
    // =========================================================================
    runtimeFunctions_["Viper.Machine.GetOS"] = types::string();
    runtimeFunctions_["Viper.Machine.GetArch"] = types::string();
    runtimeFunctions_["Viper.Machine.GetCPUCount"] = types::integer();
    runtimeFunctions_["Viper.Machine.GetMemory"] = types::integer();
    runtimeFunctions_["Viper.Machine.GetHostname"] = types::string();
    runtimeFunctions_["Viper.Machine.GetUsername"] = types::string();

    // =========================================================================
    // MATH
    // =========================================================================
    runtimeFunctions_["Viper.Math.Abs"] = types::number();
    runtimeFunctions_["Viper.Math.AbsInt"] = types::integer();
    runtimeFunctions_["Viper.Math.Acos"] = types::number();
    runtimeFunctions_["Viper.Math.Asin"] = types::number();
    runtimeFunctions_["Viper.Math.Atan"] = types::number();
    runtimeFunctions_["Viper.Math.Atan2"] = types::number();
    runtimeFunctions_["Viper.Math.Ceil"] = types::number();
    runtimeFunctions_["Viper.Math.Cos"] = types::number();
    runtimeFunctions_["Viper.Math.Cosh"] = types::number();
    runtimeFunctions_["Viper.Math.Exp"] = types::number();
    runtimeFunctions_["Viper.Math.Floor"] = types::number();
    runtimeFunctions_["Viper.Math.Log"] = types::number();
    runtimeFunctions_["Viper.Math.Log10"] = types::number();
    runtimeFunctions_["Viper.Math.Max"] = types::number();
    runtimeFunctions_["Viper.Math.MaxInt"] = types::integer();
    runtimeFunctions_["Viper.Math.Min"] = types::number();
    runtimeFunctions_["Viper.Math.MinInt"] = types::integer();
    runtimeFunctions_["Viper.Math.Pow"] = types::number();
    runtimeFunctions_["Viper.Math.Randomize"] = types::voidType();
    runtimeFunctions_["Viper.Math.Rnd"] = types::number();
    runtimeFunctions_["Viper.Math.Round"] = types::number();
    runtimeFunctions_["Viper.Math.Sign"] = types::integer();
    runtimeFunctions_["Viper.Math.Sin"] = types::number();
    runtimeFunctions_["Viper.Math.Sinh"] = types::number();
    runtimeFunctions_["Viper.Math.Sqrt"] = types::number();
    runtimeFunctions_["Viper.Math.Tan"] = types::number();
    runtimeFunctions_["Viper.Math.Tanh"] = types::number();
    runtimeFunctions_["Viper.Math.Trunc"] = types::number();
    runtimeFunctions_["Viper.Math.Clamp"] = types::number();
    runtimeFunctions_["Viper.Math.ClampInt"] = types::integer();
    runtimeFunctions_["Viper.Math.Lerp"] = types::number();

    // =========================================================================
    // NETWORK
    // =========================================================================
    runtimeFunctions_["Viper.Network.Dns.Lookup"] = types::string();
    runtimeFunctions_["Viper.Network.Dns.ReverseLookup"] = types::string();
    runtimeFunctions_["Viper.Network.Http.Get"] = types::string();
    runtimeFunctions_["Viper.Network.Http.Post"] = types::string();
    runtimeFunctions_["Viper.Network.Http.GetBytes"] = types::ptr();
    runtimeFunctions_["Viper.Network.Http.PostBytes"] = types::ptr();
    runtimeFunctions_["Viper.Network.Tcp.Connect"] = types::ptr();
    runtimeFunctions_["Viper.Network.Tcp.Close"] = types::voidType();
    runtimeFunctions_["Viper.Network.Tcp.Send"] = types::integer();
    runtimeFunctions_["Viper.Network.Tcp.Recv"] = types::ptr();
    runtimeFunctions_["Viper.Network.Tcp.Listen"] = types::ptr();
    runtimeFunctions_["Viper.Network.Tcp.Accept"] = types::ptr();
    runtimeFunctions_["Viper.Network.Udp.Open"] = types::ptr();
    runtimeFunctions_["Viper.Network.Udp.Close"] = types::voidType();
    runtimeFunctions_["Viper.Network.Udp.Send"] = types::integer();
    runtimeFunctions_["Viper.Network.Udp.Recv"] = types::ptr();
    runtimeFunctions_["Viper.Network.Url.Encode"] = types::string();
    runtimeFunctions_["Viper.Network.Url.Decode"] = types::string();
    runtimeFunctions_["Viper.Network.Url.Parse"] = types::ptr();

    // =========================================================================
    // PARSE
    // =========================================================================
    runtimeFunctions_["Viper.Parse.Int"] = types::integer();
    runtimeFunctions_["Viper.Parse.Num"] = types::number();
    runtimeFunctions_["Viper.Parse.Bool"] = types::boolean();
    runtimeFunctions_["Viper.Parse.TryInt"] = types::boolean();
    runtimeFunctions_["Viper.Parse.TryNum"] = types::boolean();

    // =========================================================================
    // RANDOM
    // =========================================================================
    runtimeFunctions_["Viper.Random.Next"] = types::number();
    runtimeFunctions_["Viper.Random.NextInt"] = types::integer();
    runtimeFunctions_["Viper.Random.Seed"] = types::voidType();
    runtimeFunctions_["Viper.Random.Range"] = types::integer();
    runtimeFunctions_["Viper.Random.RangeF"] = types::number();
    runtimeFunctions_["Viper.Random.Bool"] = types::boolean();
    runtimeFunctions_["Viper.Random.Choice"] = types::ptr();
    runtimeFunctions_["Viper.Random.Shuffle"] = types::voidType();

    // =========================================================================
    // STRING
    // =========================================================================
    runtimeFunctions_["Viper.String.Concat"] = types::string();
    runtimeFunctions_["Viper.String.Length"] = types::integer();
    runtimeFunctions_["Viper.String.Substring"] = types::string();
    runtimeFunctions_["Viper.String.Left"] = types::string();
    runtimeFunctions_["Viper.String.Right"] = types::string();
    runtimeFunctions_["Viper.String.Mid"] = types::string();
    runtimeFunctions_["Viper.String.IndexOf"] = types::integer();
    runtimeFunctions_["Viper.String.IndexOfFrom"] = types::integer();
    runtimeFunctions_["Viper.String.Has"] = types::boolean(); // VL-002: was Contains
    runtimeFunctions_["Viper.String.StartsWith"] = types::boolean();
    runtimeFunctions_["Viper.String.EndsWith"] = types::boolean();
    runtimeFunctions_["Viper.String.ToUpper"] = types::string();
    runtimeFunctions_["Viper.String.ToLower"] = types::string();
    runtimeFunctions_["Viper.String.Trim"] = types::string();
    runtimeFunctions_["Viper.String.TrimStart"] = types::string(); // VL-003: was TrimLeft
    runtimeFunctions_["Viper.String.TrimEnd"] = types::string();   // VL-003: was TrimRight
    runtimeFunctions_["Viper.String.Replace"] = types::string();
    runtimeFunctions_["Viper.String.Split"] = types::ptr();
    runtimeFunctions_["Viper.String.Repeat"] = types::string();
    runtimeFunctions_["Viper.String.Flip"] = types::string(); // VL-005: was Reverse
    runtimeFunctions_["Viper.String.Chr"] = types::string();
    runtimeFunctions_["Viper.String.Asc"] = types::integer();
    runtimeFunctions_["Viper.String.get_IsEmpty"] = types::boolean(); // VL-011: was IsEmpty
    runtimeFunctions_["Viper.String.Cmp"] = types::integer();         // VL-007: was Compare
    runtimeFunctions_["Viper.Strings.Join"] = types::string(); // VL-010: was Viper.String.Join
    runtimeFunctions_["Viper.Strings.Equals"] = types::boolean();
    runtimeFunctions_["Viper.Strings.Compare"] = types::integer();

    // =========================================================================
    // TERMINAL
    // =========================================================================
    runtimeFunctions_["Viper.Terminal.Say"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayInt"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayNum"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SayBool"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Print"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.PrintInt"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.PrintNum"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.ReadLine"] = types::string();
    runtimeFunctions_["Viper.Terminal.GetKey"] = types::string();
    runtimeFunctions_["Viper.Terminal.InKey"] = types::string();
    runtimeFunctions_["Viper.Terminal.GetKeyTimeout"] = types::string();
    runtimeFunctions_["Viper.Terminal.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Bell"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.Flush"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetColor"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetPosition"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetCursorVisible"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.SetAltScreen"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.BeginBatch"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.EndBatch"] = types::voidType();
    runtimeFunctions_["Viper.Terminal.GetWidth"] = types::integer();
    runtimeFunctions_["Viper.Terminal.GetHeight"] = types::integer();

    // =========================================================================
    // TEXT - CODEC, CSV, GUID, STRINGBUILDER, TEMPLATE, PATTERN
    // =========================================================================
    runtimeFunctions_["Viper.Text.Codec.EncodeBase64"] = types::string();
    runtimeFunctions_["Viper.Text.Codec.DecodeBase64"] = types::ptr();
    runtimeFunctions_["Viper.Text.Codec.EncodeHex"] = types::string();
    runtimeFunctions_["Viper.Text.Codec.DecodeHex"] = types::ptr();
    runtimeFunctions_["Viper.Text.Codec.EncodeUrl"] = types::string();
    runtimeFunctions_["Viper.Text.Codec.DecodeUrl"] = types::string();
    runtimeFunctions_["Viper.Text.Csv.Parse"] = types::ptr();
    runtimeFunctions_["Viper.Text.Csv.ParseFile"] = types::ptr();
    runtimeFunctions_["Viper.Text.Csv.Write"] = types::string();
    runtimeFunctions_["Viper.Text.Guid.New"] = types::string();
    runtimeFunctions_["Viper.Text.Guid.Parse"] = types::string();
    runtimeFunctions_["Viper.Text.StringBuilder.New"] = types::ptr();
    runtimeFunctions_["Viper.Text.StringBuilder.Append"] = types::voidType();
    runtimeFunctions_["Viper.Text.StringBuilder.AppendLine"] = types::voidType();
    runtimeFunctions_["Viper.Text.StringBuilder.ToString"] = types::string();
    runtimeFunctions_["Viper.Text.StringBuilder.Clear"] = types::voidType();
    runtimeFunctions_["Viper.Text.StringBuilder.get_Length"] = types::integer();
    runtimeFunctions_["Viper.Text.Template.New"] = types::ptr();
    runtimeFunctions_["Viper.Text.Template.Set"] = types::voidType();
    runtimeFunctions_["Viper.Text.Template.Render"] = types::string();
    runtimeFunctions_["Viper.Text.Pattern.New"] = types::ptr();
    runtimeFunctions_["Viper.Text.Pattern.Match"] = types::boolean();
    runtimeFunctions_["Viper.Text.Pattern.FindAll"] = types::ptr();
    runtimeFunctions_["Viper.Text.Pattern.Replace"] = types::string();

    // =========================================================================
    // THREADS
    // =========================================================================
    runtimeFunctions_["Viper.Threads.Thread.New"] = types::ptr();
    runtimeFunctions_["Viper.Threads.Thread.Start"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Thread.Join"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Thread.IsAlive"] = types::boolean();
    runtimeFunctions_["Viper.Threads.Thread.Sleep"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Thread.Yield"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Thread.GetId"] = types::integer();
    runtimeFunctions_["Viper.Threads.Barrier.New"] = types::ptr();
    runtimeFunctions_["Viper.Threads.Barrier.Wait"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Gate.New"] = types::ptr();
    runtimeFunctions_["Viper.Threads.Gate.Open"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Gate.Close"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Gate.Wait"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Monitor.New"] = types::ptr();
    runtimeFunctions_["Viper.Threads.Monitor.Enter"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Monitor.Exit"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Monitor.Wait"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Monitor.Notify"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Monitor.NotifyAll"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Semaphore.New"] = types::ptr();
    runtimeFunctions_["Viper.Threads.Semaphore.Wait"] = types::voidType();
    runtimeFunctions_["Viper.Threads.Semaphore.Signal"] = types::voidType();

    // =========================================================================
    // TIME
    // =========================================================================
    runtimeFunctions_["Viper.Time.Clock.Now"] = types::integer();
    runtimeFunctions_["Viper.Time.Clock.Millis"] = types::integer();
    runtimeFunctions_["Viper.Time.Clock.Micros"] = types::integer();
    runtimeFunctions_["Viper.Time.Clock.Sleep"] = types::voidType();
    runtimeFunctions_["Viper.Time.SleepMs"] = types::voidType();
    runtimeFunctions_["Viper.Countdown.New"] = types::ptr();
    runtimeFunctions_["Viper.Countdown.Tick"] = types::integer();
    runtimeFunctions_["Viper.Countdown.Reset"] = types::voidType();
    runtimeFunctions_["Viper.Countdown.Expired"] = types::boolean();

    // =========================================================================
    // VEC2, VEC3 (2D/3D VECTORS)
    // =========================================================================
    runtimeFunctions_["Viper.Vec2.New"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Add"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Sub"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Mul"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Div"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Dot"] = types::number();
    runtimeFunctions_["Viper.Vec2.Length"] = types::number();
    runtimeFunctions_["Viper.Vec2.Normalize"] = types::ptr();
    runtimeFunctions_["Viper.Vec2.Distance"] = types::number();
    runtimeFunctions_["Viper.Vec2.Lerp"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.New"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Add"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Sub"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Mul"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Div"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Dot"] = types::number();
    runtimeFunctions_["Viper.Vec3.Cross"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Length"] = types::number();
    runtimeFunctions_["Viper.Vec3.Normalize"] = types::ptr();
    runtimeFunctions_["Viper.Vec3.Distance"] = types::number();
    runtimeFunctions_["Viper.Vec3.Lerp"] = types::ptr();

    // =========================================================================
    // CONVERT
    // =========================================================================
    runtimeFunctions_["Viper.Convert.IntToStr"] = types::string();
    runtimeFunctions_["Viper.Convert.NumToStr"] = types::string();
    runtimeFunctions_["Viper.Convert.BoolToStr"] = types::string();
    runtimeFunctions_["Viper.Convert.StrToInt"] = types::integer();
    runtimeFunctions_["Viper.Convert.StrToNum"] = types::number();
    runtimeFunctions_["Viper.Convert.StrToBool"] = types::boolean();
    runtimeFunctions_["Viper.Convert.NumToInt"] = types::integer();
}

} // namespace il::frontends::viperlang
