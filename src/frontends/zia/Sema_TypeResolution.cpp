//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_TypeResolution.cpp
/// @brief Type resolution, extern function registration, and closure capture
/// collection for the Zia semantic analyzer.
///
/// @details This file implements:
/// - resolveNamedType: maps type names to semantic types (built-ins, registry,
///   imports, cross-module references)
/// - resolveTypeNode: resolves AST TypeNode trees to semantic TypeRefs
/// - defineExternFunction: registers runtime/extern functions in scope
/// - collectCaptures: collects captured variables from lambda bodies
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include <functional>
#include <set>

namespace il::frontends::zia {

//=============================================================================
// Type Resolution
//=============================================================================

TypeRef Sema::resolveNamedType(const std::string &name, SourceLoc useLoc) const {
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

    // Built-in collection types (default element type is unknown for non-generic usage)
    if (name == "List")
        return types::list(types::unknown());
    if (name == "Set")
        return types::set(types::unknown());
    if (name == "Map")
        return types::map(types::string(), types::unknown());

    if (Symbol *sym = const_cast<Sema *>(this)->lookupAccessibleSymbol(name, useLoc);
        sym && sym->kind == Symbol::Kind::Type) {
        return sym->type;
    }

    // Look up in registry
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end())
        return it->second;

    // Check type aliases (type Name = TargetType;)
    auto aliasIt = typeAliases_.find(name);
    if (aliasIt != typeAliases_.end())
        return aliasIt->second;

    // Check if this is an imported type from a bound namespace
    // e.g., "Canvas" imported from "Viper.Graphics"
    auto importIt = importedSymbols_.find(name);
    if (importIt != importedSymbols_.end()) {
        const std::string &fullName = importIt->second;

        // Check if the imported type is a built-in collection type
        if (fullName == "Viper.Collections.List")
            return types::list(types::unknown());
        if (fullName == "Viper.Collections.Set")
            return types::set(types::unknown());
        if (fullName == "Viper.Collections.Map")
            return types::map(types::string(), types::unknown());

        // Look up the full qualified name in the registry
        it = typeRegistry_.find(fullName);
        if (it != typeRegistry_.end())
            return it->second;

        // For runtime classes (e.g., Viper.Graphics.Canvas), return a runtime class type
        // with the full qualified name so the lowerer can generate correct calls
        if (fullName.rfind("Viper.", 0) == 0) {
            return types::runtimeClass(fullName);
        }
    }

    // Handle qualified type references (e.g., "token.Token", "Mod.Ns.Type")
    auto dotPos = name.find('.');
    if (dotPos != std::string::npos) {
        std::string prefix = name.substr(0, dotPos);
        std::string suffix = name.substr(dotPos + 1);

        auto moduleIt = moduleExports_.find(prefix);
        if (moduleIt != moduleExports_.end()) {
            std::string firstItem = suffix;
            std::string remaining;
            auto itemDot = suffix.find('.');
            if (itemDot != std::string::npos) {
                firstItem = suffix.substr(0, itemDot);
                remaining = suffix.substr(itemDot + 1);
            }

            auto exportIt = moduleIt->second.find(firstItem);
            if (exportIt != moduleIt->second.end()) {
                const Symbol &exportSym = exportIt->second;
                if (!remaining.empty() && exportSym.kind == Symbol::Kind::Module && exportSym.type)
                    return resolveNamedType(exportSym.type->name + "." + remaining, useLoc);
                if (remaining.empty() && exportSym.kind == Symbol::Kind::Type)
                    return exportSym.type;
                if (remaining.empty() && exportSym.kind == Symbol::Kind::Module)
                    return exportSym.type;
                return nullptr;
            }
            return nullptr;
        }

        // Check if prefix is a namespace alias (e.g., GUI -> Viper.GUI)
        auto prefixIt = importedSymbols_.find(prefix);
        if (prefixIt != importedSymbols_.end()) {
            std::string fullName = prefixIt->second + "." + suffix;
            it = typeRegistry_.find(fullName);
            if (it != typeRegistry_.end())
                return it->second;
            if (fullName.rfind("Viper.", 0) == 0)
                return types::runtimeClass(fullName);
        }

        // Look up the fully-qualified type name directly (used for namespaces).
        it = typeRegistry_.find(name);
        if (it != typeRegistry_.end())
            return it->second;

        // Backwards-compatible fallback: strip the module prefix and look up the base name.
        it = typeRegistry_.find(suffix);
        if (it != typeRegistry_.end())
            return it->second;
    }

    return nullptr;
}

TypeRef Sema::resolveTypeNode(const TypeNode *node) {
    if (!node)
        return types::unknown();

    if (++typeResolveDepth_ > kMaxTypeResolveDepth) {
        --typeResolveDepth_;
        error(node->loc, "type nesting too deep during resolution (limit: 256)");
        return types::unknown();
    }

    struct DepthGuard {
        unsigned &d;

        ~DepthGuard() {
            --d;
        }
    } typeGuard_{typeResolveDepth_};

    switch (node->kind) {
        case TypeKind::Named: {
            auto *named = static_cast<const NamedType *>(node);

            // Check if this is a type parameter in current generic context
            if (TypeRef substituted = lookupTypeParam(named->name))
                return substituted;

            TypeRef resolved = resolveNamedType(named->name, node->loc);
            if (!resolved) {
                error(node->loc, "Unknown type: " + named->name);
                return types::unknown();
            }
            return resolved;
        }

        case TypeKind::Generic: {
            auto *generic = static_cast<const GenericType *>(node);
            std::vector<TypeRef> args;
            for (const auto &arg : generic->args) {
                args.push_back(resolveTypeNode(arg.get()));
            }

            // Built-in generic types
            if (generic->name == "List") {
                return types::list(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Set") {
                return types::set(args.empty() ? types::unknown() : args[0]);
            }
            if (generic->name == "Map") {
                TypeRef keyType = args.size() > 0 ? args[0] : types::unknown();
                TypeRef valueType = args.size() > 1 ? args[1] : types::unknown();
                if (keyType && keyType->kind != TypeKindSem::Unknown &&
                    keyType->kind != TypeKindSem::String) {
                    error(node->loc, "Map keys must be String");
                }
                return types::map(keyType, valueType);
            }
            if (generic->name == "Result") {
                return types::result(args.empty() ? types::unit() : args[0]);
            }

            // User-defined generic type - check if registered for instantiation
            if (genericTypeDecls_.count(generic->name)) {
                return instantiateGenericType(generic->name, args, node->loc);
            }

            // Fallback: resolve as named type with type arguments
            TypeRef baseType = resolveNamedType(generic->name, node->loc);
            if (!baseType) {
                error(node->loc, "Unknown type: " + generic->name);
                return types::unknown();
            }

            // Create type with arguments (for built-in-like types)
            return std::make_shared<ViperType>(baseType->kind, baseType->name, args);
        }

        case TypeKind::Optional: {
            auto *opt = static_cast<const OptionalType *>(node);
            TypeRef inner = resolveTypeNode(opt->inner.get());
            return types::optional(inner);
        }

        case TypeKind::Function: {
            auto *func = static_cast<const FunctionType *>(node);
            std::vector<TypeRef> params;
            for (const auto &param : func->params) {
                params.push_back(resolveTypeNode(param.get()));
            }
            TypeRef ret =
                func->returnType ? resolveTypeNode(func->returnType.get()) : types::voidType();
            return types::function(params, ret);
        }

        case TypeKind::Tuple: {
            const auto *tupleType = static_cast<const TupleType *>(node);
            std::vector<TypeRef> elementTypes;
            for (const auto &elem : tupleType->elements) {
                elementTypes.push_back(resolveType(elem.get()));
            }
            return types::tuple(std::move(elementTypes));
        }

        case TypeKind::FixedArray: {
            const auto *arr = static_cast<const FixedArrayType *>(node);
            TypeRef elemType = resolveTypeNode(arr->elementType.get());
            return types::fixedArray(elemType, arr->count);
        }
    }

    return types::unknown();
}

//=============================================================================
// Extern Function Registration
//=============================================================================

void Sema::defineExternFunction(const std::string &name,
                                TypeRef returnType,
                                const std::vector<TypeRef> &paramTypes,
                                const std::vector<std::string> &paramNames) {
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = name;
    sym.type = types::function(paramTypes, returnType);
    sym.isExtern = true;
    sym.decl = nullptr; // No AST declaration for extern functions
    if (!paramNames.empty()) {
        sym.paramNames = paramNames;
    } else if (Symbol *existing = currentScope_->lookupLocal(name);
               existing && existing->isExtern && existing->kind == Symbol::Kind::Function) {
        sym.paramNames = existing->paramNames;
    }
    defineSymbol(name, std::move(sym));
}

//=============================================================================
// Closure Capture Collection
//=============================================================================

void Sema::collectCaptures(const Expr *expr,
                           const std::set<std::string> &lambdaLocals,
                           std::vector<CapturedVar> &captures) {
    if (!expr)
        return;

    std::set<std::string> captured;
    std::vector<std::set<std::string>> localScopes;
    localScopes.push_back(lambdaLocals);

    auto isLocalName = [&](const std::string &name) {
        for (auto it = localScopes.rbegin(); it != localScopes.rend(); ++it) {
            if (it->find(name) != it->end())
                return true;
        }
        return false;
    };

    auto recordCapture = [&](const std::string &name) {
        if (isLocalName(name))
            return;
        Symbol *sym = lookupSymbol(name);
        if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Parameter))
            return;
        if (!captured.insert(name).second)
            return;
        captures.push_back({name});
    };

    std::function<void(const MatchArm::Pattern &)> collectPatternBindings =
        [&](const MatchArm::Pattern &pattern) {
            switch (pattern.kind) {
                case MatchArm::Pattern::Kind::Binding:
                    if (pattern.binding != "Some" && pattern.binding != "None" &&
                        !pattern.binding.empty()) {
                        localScopes.back().insert(pattern.binding);
                    }
                    break;
                case MatchArm::Pattern::Kind::Tuple:
                case MatchArm::Pattern::Kind::Constructor:
                case MatchArm::Pattern::Kind::Or:
                    for (const auto &subpattern : pattern.subpatterns)
                        collectPatternBindings(subpattern);
                    break;
                default:
                    break;
            }
        };

    std::function<void(const Expr *)> collectExpr;
    std::function<void(const Stmt *)> collectStmt;

    collectStmt = [&](const Stmt *stmt) {
        if (!stmt)
            return;

        switch (stmt->kind) {
            case StmtKind::Block: {
                auto *block = static_cast<const BlockStmt *>(stmt);
                localScopes.push_back({});
                for (const auto &inner : block->statements)
                    collectStmt(inner.get());
                localScopes.pop_back();
                break;
            }
            case StmtKind::Expr:
                collectExpr(static_cast<const ExprStmt *>(stmt)->expr.get());
                break;
            case StmtKind::Var: {
                auto *var = static_cast<const VarStmt *>(stmt);
                if (var->initializer)
                    collectExpr(var->initializer.get());
                localScopes.back().insert(var->name);
                break;
            }
            case StmtKind::If: {
                auto *ifStmt = static_cast<const IfStmt *>(stmt);
                collectExpr(ifStmt->condition.get());
                if (ifStmt->thenBranch) {
                    localScopes.push_back({});
                    collectStmt(ifStmt->thenBranch.get());
                    localScopes.pop_back();
                }
                if (ifStmt->elseBranch) {
                    localScopes.push_back({});
                    collectStmt(ifStmt->elseBranch.get());
                    localScopes.pop_back();
                }
                break;
            }
            case StmtKind::While: {
                auto *whileStmt = static_cast<const WhileStmt *>(stmt);
                collectExpr(whileStmt->condition.get());
                localScopes.push_back({});
                collectStmt(whileStmt->body.get());
                localScopes.pop_back();
                break;
            }
            case StmtKind::For: {
                auto *forStmt = static_cast<const ForStmt *>(stmt);
                localScopes.push_back({});
                collectStmt(forStmt->init.get());
                collectExpr(forStmt->condition.get());
                collectExpr(forStmt->update.get());
                collectStmt(forStmt->body.get());
                localScopes.pop_back();
                break;
            }
            case StmtKind::ForIn: {
                auto *forIn = static_cast<const ForInStmt *>(stmt);
                localScopes.push_back({});
                collectExpr(forIn->iterable.get());
                localScopes.back().insert(forIn->variable);
                if (forIn->isTuple && !forIn->secondVariable.empty())
                    localScopes.back().insert(forIn->secondVariable);
                collectStmt(forIn->body.get());
                localScopes.pop_back();
                break;
            }
            case StmtKind::Return:
                collectExpr(static_cast<const ReturnStmt *>(stmt)->value.get());
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
            case StmtKind::Guard: {
                auto *guard = static_cast<const GuardStmt *>(stmt);
                collectExpr(guard->condition.get());
                localScopes.push_back({});
                collectStmt(guard->elseBlock.get());
                localScopes.pop_back();
                break;
            }
            case StmtKind::Match: {
                auto *match = static_cast<const MatchStmt *>(stmt);
                collectExpr(match->scrutinee.get());
                for (const auto &arm : match->arms) {
                    localScopes.push_back({});
                    collectPatternBindings(arm.pattern);
                    collectExpr(arm.pattern.guard.get());
                    collectExpr(arm.body.get());
                    localScopes.pop_back();
                }
                break;
            }
            case StmtKind::Try: {
                auto *tryStmt = static_cast<const TryStmt *>(stmt);
                localScopes.push_back({});
                collectStmt(tryStmt->tryBody.get());
                localScopes.pop_back();
                if (tryStmt->catchBody) {
                    localScopes.push_back({});
                    if (!tryStmt->catchVar.empty())
                        localScopes.back().insert(tryStmt->catchVar);
                    collectStmt(tryStmt->catchBody.get());
                    localScopes.pop_back();
                }
                if (tryStmt->finallyBody) {
                    localScopes.push_back({});
                    collectStmt(tryStmt->finallyBody.get());
                    localScopes.pop_back();
                }
                break;
            }
            case StmtKind::Throw:
                collectExpr(static_cast<const ThrowStmt *>(stmt)->value.get());
                break;
        }
    };

    collectExpr = [&](const Expr *e) {
        if (!e)
            return;

        switch (e->kind) {
            case ExprKind::Ident:
                recordCapture(static_cast<const IdentExpr *>(e)->name);
                break;
            case ExprKind::Binary: {
                auto *bin = static_cast<const BinaryExpr *>(e);
                collectExpr(bin->left.get());
                collectExpr(bin->right.get());
                break;
            }
            case ExprKind::Unary:
                collectExpr(static_cast<const UnaryExpr *>(e)->operand.get());
                break;
            case ExprKind::Ternary: {
                auto *ternary = static_cast<const TernaryExpr *>(e);
                collectExpr(ternary->condition.get());
                collectExpr(ternary->thenExpr.get());
                collectExpr(ternary->elseExpr.get());
                break;
            }
            case ExprKind::Call: {
                auto *call = static_cast<const CallExpr *>(e);
                collectExpr(call->callee.get());
                for (const auto &arg : call->args)
                    collectExpr(arg.value.get());
                break;
            }
            case ExprKind::Index: {
                auto *idx = static_cast<const IndexExpr *>(e);
                collectExpr(idx->base.get());
                collectExpr(idx->index.get());
                break;
            }
            case ExprKind::Field:
                collectExpr(static_cast<const FieldExpr *>(e)->base.get());
                break;
            case ExprKind::OptionalChain:
                collectExpr(static_cast<const OptionalChainExpr *>(e)->base.get());
                break;
            case ExprKind::Coalesce: {
                auto *coalesce = static_cast<const CoalesceExpr *>(e);
                collectExpr(coalesce->left.get());
                collectExpr(coalesce->right.get());
                break;
            }
            case ExprKind::Is:
                collectExpr(static_cast<const IsExpr *>(e)->value.get());
                break;
            case ExprKind::As:
                collectExpr(static_cast<const AsExpr *>(e)->value.get());
                break;
            case ExprKind::Range: {
                auto *range = static_cast<const RangeExpr *>(e);
                collectExpr(range->start.get());
                collectExpr(range->end.get());
                break;
            }
            case ExprKind::Try:
                collectExpr(static_cast<const TryExpr *>(e)->operand.get());
                break;
            case ExprKind::ForceUnwrap:
                collectExpr(static_cast<const ForceUnwrapExpr *>(e)->operand.get());
                break;
            case ExprKind::Await:
                collectExpr(static_cast<const AwaitExpr *>(e)->operand.get());
                break;
            case ExprKind::New: {
                auto *created = static_cast<const NewExpr *>(e);
                for (const auto &arg : created->args)
                    collectExpr(arg.value.get());
                break;
            }
            case ExprKind::StructLiteral: {
                auto *literal = static_cast<const StructLiteralExpr *>(e);
                for (const auto &field : literal->fields)
                    collectExpr(field.value.get());
                break;
            }
            case ExprKind::Lambda:
                break;
            case ExprKind::ListLiteral: {
                auto *list = static_cast<const ListLiteralExpr *>(e);
                for (const auto &elem : list->elements)
                    collectExpr(elem.get());
                break;
            }
            case ExprKind::MapLiteral: {
                auto *map = static_cast<const MapLiteralExpr *>(e);
                for (const auto &entry : map->entries) {
                    collectExpr(entry.key.get());
                    collectExpr(entry.value.get());
                }
                break;
            }
            case ExprKind::SetLiteral: {
                auto *set = static_cast<const SetLiteralExpr *>(e);
                for (const auto &elem : set->elements)
                    collectExpr(elem.get());
                break;
            }
            case ExprKind::Tuple: {
                auto *tuple = static_cast<const TupleExpr *>(e);
                for (const auto &elem : tuple->elements)
                    collectExpr(elem.get());
                break;
            }
            case ExprKind::TupleIndex:
                collectExpr(static_cast<const TupleIndexExpr *>(e)->tuple.get());
                break;
            case ExprKind::If: {
                auto *ifExpr = static_cast<const IfExpr *>(e);
                collectExpr(ifExpr->condition.get());
                collectExpr(ifExpr->thenBranch.get());
                collectExpr(ifExpr->elseBranch.get());
                break;
            }
            case ExprKind::Match: {
                auto *match = static_cast<const MatchExpr *>(e);
                collectExpr(match->scrutinee.get());
                for (const auto &arm : match->arms) {
                    localScopes.push_back({});
                    collectPatternBindings(arm.pattern);
                    collectExpr(arm.pattern.guard.get());
                    collectExpr(arm.body.get());
                    localScopes.pop_back();
                }
                break;
            }
            case ExprKind::Block: {
                auto *block = static_cast<const BlockExpr *>(e);
                localScopes.push_back({});
                for (const auto &stmt : block->statements)
                    collectStmt(stmt.get());
                collectExpr(block->value.get());
                localScopes.pop_back();
                break;
            }
            default:
                break;
        }
    };

    collectExpr(expr);
}

} // namespace il::frontends::zia
