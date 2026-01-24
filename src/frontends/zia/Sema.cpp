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
/// @details This file implements the Sema class which performs type checking
/// and name resolution on Zia ASTs. Key implementation details:
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

#include "frontends/zia/Sema.hpp"
#include <cassert>
#include <functional>
#include <set>
#include <sstream>

namespace il::frontends::zia
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
// Type Parameter Substitution Implementation
//=============================================================================

void Sema::pushTypeParams(const std::map<std::string, TypeRef> &substitutions)
{
    typeParamStack_.push_back(substitutions);
}

void Sema::popTypeParams()
{
    assert(!typeParamStack_.empty() && "Unbalanced type param stack");
    typeParamStack_.pop_back();
}

TypeRef Sema::lookupTypeParam(const std::string &name) const
{
    // Search from innermost to outermost scope
    for (auto it = typeParamStack_.rbegin(); it != typeParamStack_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
            return found->second;
    }
    return nullptr; // Not found - remains unsubstituted
}

TypeRef Sema::substituteTypeParams(TypeRef type) const
{
    if (!type || typeParamStack_.empty())
        return type;

    // If this is a type parameter, look it up
    if (type->kind == TypeKindSem::TypeParam)
    {
        if (auto subst = lookupTypeParam(type->name))
            return subst;
        return type;
    }

    // For generic types with type arguments, substitute each argument
    if (!type->typeArgs.empty())
    {
        std::vector<TypeRef> newArgs;
        newArgs.reserve(type->typeArgs.size());
        bool changed = false;
        for (const auto &arg : type->typeArgs)
        {
            TypeRef substArg = substituteTypeParams(arg);
            if (substArg != arg)
                changed = true;
            newArgs.push_back(substArg);
        }
        if (changed)
        {
            return std::make_shared<ViperType>(type->kind, type->name, newArgs);
        }
    }

    // For function types, substitute return and param types
    if (type->kind == TypeKindSem::Function)
    {
        TypeRef newReturn = substituteTypeParams(type->returnType());
        std::vector<TypeRef> newParams;
        bool changed = (newReturn != type->returnType());
        for (const auto &p : type->paramTypes())
        {
            TypeRef substParam = substituteTypeParams(p);
            if (substParam != p)
                changed = true;
            newParams.push_back(substParam);
        }
        if (changed)
        {
            return types::function(newParams, newReturn);
        }
    }

    // For optional types, substitute inner type
    if (type->kind == TypeKindSem::Optional)
    {
        TypeRef inner = substituteTypeParams(type->innerType());
        if (inner != type->innerType())
        {
            return types::optional(inner);
        }
    }

    return type;
}

std::string Sema::mangleGenericName(const std::string &base, const std::vector<TypeRef> &args)
{
    std::string result = base;
    for (const auto &arg : args)
    {
        result += "$";
        if (arg && !arg->name.empty())
            result += arg->name;
        else if (arg)
            result += kindToString(arg->kind);
        else
            result += "unknown";
    }
    return result;
}

void Sema::registerGenericType(const std::string &name, Decl *decl)
{
    genericTypeDecls_[name] = decl;
}

std::vector<std::string> Sema::getGenericParams(const Decl *decl)
{
    switch (decl->kind)
    {
        case DeclKind::Value:
            return static_cast<const ValueDecl *>(decl)->genericParams;
        case DeclKind::Entity:
            return static_cast<const EntityDecl *>(decl)->genericParams;
        case DeclKind::Interface:
            return static_cast<const InterfaceDecl *>(decl)->genericParams;
        case DeclKind::Function:
            return static_cast<const FunctionDecl *>(decl)->genericParams;
        default:
            return {};
    }
}

TypeRef Sema::analyzeGenericTypeBody(Decl *decl, const std::string &mangledName)
{
    // Create the instantiated type based on declaration kind
    switch (decl->kind)
    {
        case DeclKind::Value:
        {
            auto *valueDecl = static_cast<ValueDecl *>(decl);
            // Create the instantiated value type
            auto instantiated = std::make_shared<ViperType>(TypeKindSem::Value, mangledName);

            // Register the instantiated type first so self-references work
            typeRegistry_[mangledName] = instantiated;

            // Analyze members with substitutions active
            for (const auto &member : valueDecl->members)
            {
                if (member->kind == DeclKind::Field)
                {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    TypeRef fieldType = resolveTypeNode(field->type.get());
                    std::string key = mangledName + "." + field->name;
                    fieldTypes_[key] = fieldType;
                }
                else if (member->kind == DeclKind::Method)
                {
                    auto *method = static_cast<MethodDecl *>(member.get());
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : method->params)
                    {
                        paramTypes.push_back(resolveTypeNode(param.type.get()));
                    }
                    TypeRef returnType = method->returnType
                                             ? resolveTypeNode(method->returnType.get())
                                             : types::voidType();
                    std::string key = mangledName + "." + method->name;
                    methodTypes_[key] = types::function(paramTypes, returnType);
                }
            }

            return instantiated;
        }
        case DeclKind::Entity:
        {
            auto *entityDecl = static_cast<EntityDecl *>(decl);
            auto instantiated = std::make_shared<ViperType>(TypeKindSem::Entity, mangledName);

            typeRegistry_[mangledName] = instantiated;

            for (const auto &member : entityDecl->members)
            {
                if (member->kind == DeclKind::Field)
                {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    TypeRef fieldType = resolveTypeNode(field->type.get());
                    std::string key = mangledName + "." + field->name;
                    fieldTypes_[key] = fieldType;
                }
                else if (member->kind == DeclKind::Method)
                {
                    auto *method = static_cast<MethodDecl *>(member.get());
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : method->params)
                    {
                        paramTypes.push_back(resolveTypeNode(param.type.get()));
                    }
                    TypeRef returnType = method->returnType
                                             ? resolveTypeNode(method->returnType.get())
                                             : types::voidType();
                    std::string key = mangledName + "." + method->name;
                    methodTypes_[key] = types::function(paramTypes, returnType);
                }
            }

            return instantiated;
        }
        default:
            return types::unknown();
    }
}

TypeRef Sema::instantiateGenericType(const std::string &name,
                                     const std::vector<TypeRef> &args,
                                     SourceLoc loc)
{
    // Check cache first
    std::string mangledName = mangleGenericName(name, args);
    auto cached = genericInstances_.find(mangledName);
    if (cached != genericInstances_.end())
    {
        return cached->second;
    }

    // Find original generic declaration
    auto declIt = genericTypeDecls_.find(name);
    if (declIt == genericTypeDecls_.end())
    {
        error(loc, "Unknown generic type: " + name);
        return types::unknown();
    }

    // Get generic parameters
    const auto &genericParams = getGenericParams(declIt->second);

    if (args.size() != genericParams.size())
    {
        error(loc,
              "Generic type " + name + " expects " + std::to_string(genericParams.size()) +
                  " type arguments, got " + std::to_string(args.size()));
        return types::unknown();
    }

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < genericParams.size(); ++i)
    {
        substitutions[genericParams[i]] = args[i];
    }

    // Push substitution context and analyze type body
    pushTypeParams(substitutions);
    TypeRef instantiated = analyzeGenericTypeBody(declIt->second, mangledName);
    popTypeParams();

    // Cache and return
    genericInstances_[mangledName] = instantiated;
    return instantiated;
}

void Sema::registerGenericFunction(const std::string &name, FunctionDecl *decl)
{
    genericFunctionDecls_[name] = decl;
}

bool Sema::isGenericFunction(const std::string &name) const
{
    return genericFunctionDecls_.count(name) > 0;
}

FunctionDecl *Sema::getGenericFunction(const std::string &name) const
{
    auto it = genericFunctionDecls_.find(name);
    return it != genericFunctionDecls_.end() ? it->second : nullptr;
}

bool Sema::typeImplementsInterface(TypeRef type, const std::string &interfaceName) const
{
    if (!type)
        return false;

    // Check if the type is an entity type
    if (type->kind == TypeKindSem::Entity)
    {
        auto entityIt = entityDecls_.find(type->name);
        if (entityIt != entityDecls_.end())
        {
            for (const auto &iface : entityIt->second->interfaces)
            {
                if (iface == interfaceName)
                    return true;
            }
        }
    }
    // Check if the type is a value type
    else if (type->kind == TypeKindSem::Value)
    {
        auto valueIt = valueDecls_.find(type->name);
        if (valueIt != valueDecls_.end())
        {
            for (const auto &iface : valueIt->second->interfaces)
            {
                if (iface == interfaceName)
                    return true;
            }
        }
    }

    return false;
}

TypeRef Sema::instantiateGenericFunction(const std::string &name,
                                         const std::vector<TypeRef> &args,
                                         SourceLoc loc)
{
    // Check cache first
    std::string mangledName = mangleGenericName(name, args);
    auto cached = genericFunctionInstances_.find(mangledName);
    if (cached != genericFunctionInstances_.end())
    {
        return cached->second;
    }

    // Find original generic declaration
    auto declIt = genericFunctionDecls_.find(name);
    if (declIt == genericFunctionDecls_.end())
    {
        error(loc, "Unknown generic function: " + name);
        return types::unknown();
    }

    FunctionDecl *funcDecl = declIt->second;

    // Check argument count
    if (args.size() != funcDecl->genericParams.size())
    {
        error(loc,
              "Generic function " + name + " expects " +
                  std::to_string(funcDecl->genericParams.size()) + " type arguments, got " +
                  std::to_string(args.size()));
        return types::unknown();
    }

    // Validate constraints
    for (size_t i = 0; i < args.size(); ++i)
    {
        // Check if this type parameter has a constraint
        if (i < funcDecl->genericParamConstraints.size() &&
            !funcDecl->genericParamConstraints[i].empty())
        {
            const std::string &constraintName = funcDecl->genericParamConstraints[i];
            TypeRef argType = args[i];

            // Check if the type implements the required interface
            if (!typeImplementsInterface(argType, constraintName))
            {
                error(loc,
                      "Type '" + (argType ? argType->name : "unknown") +
                          "' does not implement interface '" + constraintName +
                          "' required by type parameter '" + funcDecl->genericParams[i] + "'");
                return types::unknown();
            }
        }
    }

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < funcDecl->genericParams.size(); ++i)
    {
        substitutions[funcDecl->genericParams[i]] = args[i];
    }

    // Push substitution context and analyze function signature
    pushTypeParams(substitutions);

    // Build parameter types with substitution
    std::vector<TypeRef> paramTypes;
    for (const auto &param : funcDecl->params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
        paramTypes.push_back(paramType);
    }

    // Build return type with substitution
    TypeRef returnType =
        funcDecl->returnType ? resolveTypeNode(funcDecl->returnType.get()) : types::voidType();

    popTypeParams();

    // Create instantiated function type
    TypeRef instantiatedType = types::function(paramTypes, returnType);

    // Cache the result
    genericFunctionInstances_[mangledName] = instantiatedType;

    // Register the instantiated function as a symbol so it can be called
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = mangledName;
    sym.type = instantiatedType;
    sym.decl = funcDecl;
    defineSymbol(mangledName, sym);

    return instantiatedType;
}

bool Sema::pushSubstitutionContext(const std::string &mangledName)
{
    // Check if this is an instantiated generic (contains $)
    size_t dollarPos = mangledName.find('$');
    if (dollarPos == std::string::npos)
        return false;

    // Extract base name and type argument names
    std::string baseName = mangledName.substr(0, dollarPos);
    std::vector<std::string> typeArgNames;
    size_t pos = dollarPos;
    while (pos != std::string::npos)
    {
        size_t nextDollar = mangledName.find('$', pos + 1);
        if (nextDollar == std::string::npos)
        {
            typeArgNames.push_back(mangledName.substr(pos + 1));
            break;
        }
        else
        {
            typeArgNames.push_back(mangledName.substr(pos + 1, nextDollar - pos - 1));
            pos = nextDollar;
        }
    }

    // Look up the generic declaration (could be a type or function)
    std::vector<std::string> genericParams;
    auto typeIt = genericTypeDecls_.find(baseName);
    if (typeIt != genericTypeDecls_.end())
    {
        genericParams = getGenericParams(typeIt->second);
    }
    else
    {
        auto funcIt = genericFunctionDecls_.find(baseName);
        if (funcIt != genericFunctionDecls_.end())
        {
            genericParams = funcIt->second->genericParams;
        }
        else
        {
            return false;
        }
    }

    if (typeArgNames.size() != genericParams.size())
        return false;

    // Resolve type arguments and build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < genericParams.size(); ++i)
    {
        // Resolve the type argument by name
        TypeRef argType = resolveNamedType(typeArgNames[i]);
        if (!argType)
            argType = types::unknown();
        substitutions[genericParams[i]] = argType;
    }

    // Push the substitution context
    pushTypeParams(substitutions);
    return true;
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
    if (it == exprTypes_.end())
        return types::unknown();
    // Apply type parameter substitution if in generic context
    return substituteTypeParams(it->second);
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

Symbol *Sema::lookupSymbol(const std::string &name)
{
    return currentScope_->lookup(name);
}

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

void Sema::pushNarrowingScope()
{
    narrowedTypes_.push_back({});
}

void Sema::popNarrowingScope()
{
    if (!narrowedTypes_.empty())
    {
        narrowedTypes_.pop_back();
    }
}

void Sema::narrowType(const std::string &name, TypeRef narrowedType)
{
    if (!narrowedTypes_.empty())
    {
        narrowedTypes_.back()[name] = narrowedType;
    }
}

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
} // namespace il::frontends::zia
