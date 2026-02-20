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

namespace il::frontends::zia
{

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

    // Built-in collection types (default element type is unknown for non-generic usage)
    if (name == "List")
        return types::list(types::unknown());
    if (name == "Set")
        return types::set(types::unknown());
    if (name == "Map")
        return types::map(types::string(), types::unknown());

    // Look up in registry
    auto it = typeRegistry_.find(name);
    if (it != typeRegistry_.end())
        return it->second;

    // Check if this is an imported type from a bound namespace
    // e.g., "Canvas" imported from "Viper.Graphics"
    auto importIt = importedSymbols_.find(name);
    if (importIt != importedSymbols_.end())
    {
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
        if (fullName.rfind("Viper.", 0) == 0)
        {
            return types::runtimeClass(fullName);
        }
    }

    // Handle cross-module type references (e.g., "token.Token")
    // The ImportResolver merges imported declarations, so we just need
    // to strip the module prefix and look up the base type name.
    auto dotPos = name.find('.');
    if (dotPos != std::string::npos)
    {
        std::string prefix = name.substr(0, dotPos);
        std::string suffix = name.substr(dotPos + 1);

        // Check if prefix is a namespace alias (e.g., GUI -> Viper.GUI)
        auto prefixIt = importedSymbols_.find(prefix);
        if (prefixIt != importedSymbols_.end())
        {
            std::string fullName = prefixIt->second + "." + suffix;
            it = typeRegistry_.find(fullName);
            if (it != typeRegistry_.end())
                return it->second;
            if (fullName.rfind("Viper.", 0) == 0)
                return types::runtimeClass(fullName);
        }

        // Look up the unqualified type name in the registry
        it = typeRegistry_.find(suffix);
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

            // Check if this is a type parameter in current generic context
            if (TypeRef substituted = lookupTypeParam(named->name))
                return substituted;

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

            // User-defined generic type - check if registered for instantiation
            if (genericTypeDecls_.count(generic->name))
            {
                return instantiateGenericType(generic->name, args, node->loc);
            }

            // Fallback: resolve as named type with type arguments
            TypeRef baseType = resolveNamedType(generic->name);
            if (!baseType)
            {
                error(node->loc, "Unknown type: " + generic->name);
                return types::unknown();
            }

            // Create type with arguments (for built-in-like types)
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

        case TypeKind::FixedArray:
        {
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
                                const std::vector<TypeRef> &paramTypes)
{
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = name;
    // Create full function type if param types provided, otherwise just return type
    if (paramTypes.empty())
    {
        sym.type = returnType;
    }
    else
    {
        sym.type = types::function(paramTypes, returnType);
    }
    sym.isExtern = true;
    sym.decl = nullptr; // No AST declaration for extern functions
    defineSymbol(name, std::move(sym));
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

} // namespace il::frontends::zia
