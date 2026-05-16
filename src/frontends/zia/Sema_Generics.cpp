//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Generics.cpp
/// @brief Generic type and function support for the Zia semantic analyzer.
///
/// @details This file implements all generic-related methods of the Sema class:
/// type parameter substitution, generic type/function registration,
/// instantiation, and interface constraint checking.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"
#include <cassert>
#include <cctype>

namespace il::frontends::zia {

namespace {

std::string sanitizeMangledPart(std::string text) {
    for (char &ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '.')
            ch = '_';
    }
    return text;
}

std::string mangleTypeArg(TypeRef type) {
    if (!type)
        return "unknown";

    std::string result = !type->name.empty() ? type->name : kindToString(type->kind);
    result = sanitizeMangledPart(result);

    for (const auto &arg : type->typeArgs) {
        result += "_";
        result += mangleTypeArg(arg);
    }

    if (type->kind == TypeKindSem::FixedArray) {
        result += "_";
        result += std::to_string(type->elementCount);
    }

    return result.empty() ? "unknown" : result;
}

} // namespace

//=============================================================================
// Type Parameter Substitution Implementation
//=============================================================================

void Sema::pushTypeParams(const std::map<std::string, TypeRef> &substitutions) {
    typeParamStack_.push_back(substitutions);
}

void Sema::popTypeParams() {
    assert(!typeParamStack_.empty() && "Unbalanced type param stack");
    typeParamStack_.pop_back();
}

TypeRef Sema::lookupTypeParam(const std::string &name) const {
    // Search from innermost to outermost scope
    for (auto it = typeParamStack_.rbegin(); it != typeParamStack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end())
            return found->second;
    }
    return nullptr; // Not found - remains unsubstituted
}

TypeRef Sema::substituteTypeParams(TypeRef type) const {
    if (!type || typeParamStack_.empty())
        return type;

    // If this is a type parameter, look it up
    if (type->kind == TypeKindSem::TypeParam) {
        if (auto subst = lookupTypeParam(type->name))
            return subst;
        return type;
    }

    // For generic types with type arguments, substitute each argument
    if (!type->typeArgs.empty()) {
        std::vector<TypeRef> newArgs;
        newArgs.reserve(type->typeArgs.size());
        bool changed = false;
        for (const auto &arg : type->typeArgs) {
            TypeRef substArg = substituteTypeParams(arg);
            if (substArg != arg)
                changed = true;
            newArgs.push_back(substArg);
        }
        if (changed) {
            return std::make_shared<ViperType>(type->kind, type->name, newArgs);
        }
    }

    // For function types, substitute return and param types
    if (type->kind == TypeKindSem::Function) {
        TypeRef newReturn = substituteTypeParams(type->returnType());
        std::vector<TypeRef> newParams;
        bool changed = (newReturn != type->returnType());
        for (const auto &p : type->paramTypes()) {
            TypeRef substParam = substituteTypeParams(p);
            if (substParam != p)
                changed = true;
            newParams.push_back(substParam);
        }
        if (changed) {
            return types::function(newParams, newReturn);
        }
    }

    // For optional types, substitute inner type
    if (type->kind == TypeKindSem::Optional) {
        TypeRef inner = substituteTypeParams(type->innerType());
        if (inner != type->innerType()) {
            return types::optional(inner);
        }
    }

    return type;
}

std::string Sema::mangleGenericName(const std::string &base, const std::vector<TypeRef> &args) {
    std::string result = base;
    for (const auto &arg : args) {
        result += "$";
        result += mangleTypeArg(arg);
    }
    return result;
}

void Sema::registerGenericType(const std::string &name, Decl *decl) {
    genericTypeDecls_[name] = decl;
}

std::vector<std::string> Sema::getGenericParams(const Decl *decl) {
    switch (decl->kind) {
        case DeclKind::Struct:
            return static_cast<const StructDecl *>(decl)->genericParams;
        case DeclKind::Class:
            return static_cast<const ClassDecl *>(decl)->genericParams;
        case DeclKind::Interface:
            return static_cast<const InterfaceDecl *>(decl)->genericParams;
        case DeclKind::Function:
            return static_cast<const FunctionDecl *>(decl)->genericParams;
        default:
            return {};
    }
}

std::vector<std::string> Sema::getGenericParamConstraints(const Decl *decl) {
    switch (decl->kind) {
        case DeclKind::Struct:
            return static_cast<const StructDecl *>(decl)->genericParamConstraints;
        case DeclKind::Class:
            return static_cast<const ClassDecl *>(decl)->genericParamConstraints;
        case DeclKind::Interface:
            return static_cast<const InterfaceDecl *>(decl)->genericParamConstraints;
        case DeclKind::Function:
            return static_cast<const FunctionDecl *>(decl)->genericParamConstraints;
        case DeclKind::Method:
            return static_cast<const MethodDecl *>(decl)->genericParamConstraints;
        default:
            return {};
    }
}

bool Sema::validateGenericConstraints(const std::vector<std::string> &params,
                                      const std::vector<std::string> &constraints,
                                      const std::vector<TypeRef> &args,
                                      SourceLoc loc,
                                      const std::string &subjectName) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i >= constraints.size() || constraints[i].empty())
            continue;

        TypeRef argType = args[i];
        if (!typeImplementsInterface(argType, constraints[i])) {
            error(loc,
                  "Type '" + (argType ? argType->toDisplayString() : "unknown") +
                      "' does not implement interface '" + constraints[i] +
                      "' required by type parameter '" +
                      (i < params.size() ? params[i] : std::to_string(i)) + "' of '" +
                      subjectName + "'");
            return false;
        }
    }
    return true;
}

TypeRef Sema::analyzeGenericTypeBody(Decl *decl, const std::string &mangledName) {
    // Create the instantiated type based on declaration kind
    switch (decl->kind) {
        case DeclKind::Struct: {
            auto *structDecl = static_cast<StructDecl *>(decl);
            // Create the instantiated struct type
            auto instantiated = std::make_shared<ViperType>(TypeKindSem::Struct, mangledName);

            // Register the instantiated type first so self-references work
            typeRegistry_[mangledName] = instantiated;
            structDecls_[mangledName] = structDecl;

            // Analyze members with substitutions active
            for (const auto &member : structDecl->members) {
                if (member->kind == DeclKind::Field) {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    TypeRef fieldType = resolveTypeNode(field->type.get());
                    std::string key = mangledName + "." + field->name;
                    fieldTypes_[key] = fieldType;
                    if (field->isStatic)
                        staticFields_.insert(key);
                    if (field->isFinal)
                        finalFields_.insert(key);
                    memberVisibility_[key] = field->visibility;
                } else if (member->kind == DeclKind::Method) {
                    auto *method = static_cast<MethodDecl *>(member.get());
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : method->params) {
                        paramTypes.push_back(resolveTypeNode(param.type.get()));
                    }
                    TypeRef returnType = method->returnType
                                             ? resolveTypeNode(method->returnType.get())
                                             : types::voidType();
                    TypeRef methodType = types::function(paramTypes, returnType);
                    if (!registerMethodOverload(mangledName, method, methodType, method->loc))
                        continue;
                    std::string key = mangledName + "." + method->name;
                    if (methodTypes_.find(key) == methodTypes_.end())
                        methodTypes_[key] = methodType;
                    memberVisibility_[key] = method->visibility;
                }
            }

            return instantiated;
        }
        case DeclKind::Class: {
            auto *classDecl = static_cast<ClassDecl *>(decl);
            auto instantiated = std::make_shared<ViperType>(TypeKindSem::Class, mangledName);

            typeRegistry_[mangledName] = instantiated;
            classDecls_[mangledName] = classDecl;
            for (const auto &iface : classDecl->interfaces)
                types::registerInterfaceImplementation(mangledName, iface);
            if (!classDecl->baseClass.empty())
                types::registerClassInheritance(mangledName, classDecl->baseClass);

            for (const auto &member : classDecl->members) {
                if (member->kind == DeclKind::Field) {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    TypeRef fieldType = resolveTypeNode(field->type.get());
                    std::string key = mangledName + "." + field->name;
                    fieldTypes_[key] = fieldType;
                    if (field->isStatic)
                        staticFields_.insert(key);
                    if (field->isFinal)
                        finalFields_.insert(key);
                    memberVisibility_[key] = field->visibility;
                } else if (member->kind == DeclKind::Method) {
                    auto *method = static_cast<MethodDecl *>(member.get());
                    std::vector<TypeRef> paramTypes;
                    for (const auto &param : method->params) {
                        paramTypes.push_back(resolveTypeNode(param.type.get()));
                    }
                    TypeRef returnType = method->returnType
                                             ? resolveTypeNode(method->returnType.get())
                                             : types::voidType();
                    TypeRef methodType = types::function(paramTypes, returnType);
                    if (!registerMethodOverload(mangledName, method, methodType, method->loc))
                        continue;
                    std::string key = mangledName + "." + method->name;
                    if (methodTypes_.find(key) == methodTypes_.end())
                        methodTypes_[key] = methodType;
                    memberVisibility_[key] = method->visibility;
                }
            }

            return instantiated;
        }
        case DeclKind::Interface: {
            auto *ifaceDecl = static_cast<InterfaceDecl *>(decl);
            auto instantiated = std::make_shared<ViperType>(TypeKindSem::Interface, mangledName);

            typeRegistry_[mangledName] = instantiated;
            interfaceDecls_[mangledName] = ifaceDecl;

            for (const auto &member : ifaceDecl->members) {
                if (member->kind != DeclKind::Method)
                    continue;
                auto *method = static_cast<MethodDecl *>(member.get());
                std::vector<TypeRef> paramTypes;
                for (const auto &param : method->params)
                    paramTypes.push_back(resolveTypeNode(param.type.get()));
                TypeRef returnType = method->returnType ? resolveTypeNode(method->returnType.get())
                                                        : types::voidType();
                TypeRef methodType = types::function(paramTypes, returnType);
                if (!registerMethodOverload(mangledName, method, methodType, method->loc))
                    continue;
                std::string key = mangledName + "." + method->name;
                if (methodTypes_.find(key) == methodTypes_.end())
                    methodTypes_[key] = methodType;
                memberVisibility_[key] = method->visibility;
            }

            return instantiated;
        }
        default:
            return types::unknown();
    }
}

TypeRef Sema::instantiateGenericType(const std::string &name,
                                     const std::vector<TypeRef> &args,
                                     SourceLoc loc) {
    // Check cache first
    std::string mangledName = mangleGenericName(name, args);
    auto cached = genericInstances_.find(mangledName);
    if (cached != genericInstances_.end()) {
        return cached->second;
    }

    // Find original generic declaration
    auto declIt = genericTypeDecls_.find(name);
    if (declIt == genericTypeDecls_.end()) {
        error(loc, "Unknown generic type: " + name);
        return types::unknown();
    }

    // Get generic parameters
    const auto &genericParams = getGenericParams(declIt->second);

    if (args.size() != genericParams.size()) {
        error(loc,
              "Generic type " + name + " expects " + std::to_string(genericParams.size()) +
                  " type arguments, got " + std::to_string(args.size()));
        return types::unknown();
    }

    const auto constraints = getGenericParamConstraints(declIt->second);
    if (!validateGenericConstraints(genericParams, constraints, args, loc, name))
        return types::unknown();

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < genericParams.size(); ++i) {
        substitutions[genericParams[i]] = args[i];
    }
    genericTypeSubstitutions_[mangledName] = substitutions;

    // Push substitution context and analyze type body
    pushTypeParams(substitutions);
    TypeRef instantiated = analyzeGenericTypeBody(declIt->second, mangledName);
    popTypeParams();

    // Cache and return
    genericInstances_[mangledName] = instantiated;
    return instantiated;
}

void Sema::registerGenericFunction(const std::string &name, FunctionDecl *decl) {
    genericFunctionDecls_[name] = decl;
}

bool Sema::isGenericFunction(const std::string &name) const {
    return genericFunctionDecls_.count(name) > 0;
}

FunctionDecl *Sema::getGenericFunction(const std::string &name) const {
    auto it = genericFunctionDecls_.find(name);
    return it != genericFunctionDecls_.end() ? it->second : nullptr;
}

FunctionDecl *Sema::getFunctionDecl(const std::string &name) const {
    auto it = functionDecls_.find(name);
    return it != functionDecls_.end() ? it->second : nullptr;
}

std::vector<FunctionDecl *> Sema::getFunctionOverloads(const std::string &name) const {
    auto it = functionOverloads_.find(name);
    if (it == functionOverloads_.end())
        return {};
    return it->second;
}

bool Sema::typeImplementsInterface(TypeRef type, const std::string &interfaceName) const {
    if (!type)
        return false;

    std::string resolvedInterfaceName = interfaceName;
    if (TypeRef ifaceType = resolveNamedType(interfaceName);
        ifaceType && ifaceType->kind == TypeKindSem::Interface)
        resolvedInterfaceName = ifaceType->name;

    if (type->kind == TypeKindSem::Interface)
        return type->name == resolvedInterfaceName;

    // Check if the type is an class type
    if (type->kind == TypeKindSem::Class) {
        if (auto *classDecl = lookupClassDeclForType(type->name)) {
            for (const auto &iface : classDecl->interfaces) {
                std::string resolvedIface = iface;
                if (TypeRef ifaceType = resolveNamedType(iface);
                    ifaceType && ifaceType->kind == TypeKindSem::Interface)
                    resolvedIface = ifaceType->name;
                if (resolvedIface == resolvedInterfaceName)
                    return true;
            }
        }
    }
    // Check if the type is a struct type
    else if (type->kind == TypeKindSem::Struct) {
        if (auto *structDecl = lookupStructDeclForType(type->name)) {
            for (const auto &iface : structDecl->interfaces) {
                std::string resolvedIface = iface;
                if (TypeRef ifaceType = resolveNamedType(iface);
                    ifaceType && ifaceType->kind == TypeKindSem::Interface)
                    resolvedIface = ifaceType->name;
                if (resolvedIface == resolvedInterfaceName)
                    return true;
            }
        }
    }

    return false;
}

TypeRef Sema::instantiateGenericFunction(const std::string &name,
                                         const std::vector<TypeRef> &args,
                                         SourceLoc loc) {
    // Check cache first
    std::string mangledName = mangleGenericName(name, args);
    auto cached = genericFunctionInstances_.find(mangledName);
    if (cached != genericFunctionInstances_.end()) {
        return cached->second;
    }

    // Find original generic declaration
    auto declIt = genericFunctionDecls_.find(name);
    if (declIt == genericFunctionDecls_.end()) {
        error(loc, "Unknown generic function: " + name);
        return types::unknown();
    }

    FunctionDecl *funcDecl = declIt->second;

    // Check argument count
    if (args.size() != funcDecl->genericParams.size()) {
        error(loc,
              "Generic function " + name + " expects " +
                  std::to_string(funcDecl->genericParams.size()) + " type arguments, got " +
                  std::to_string(args.size()));
        return types::unknown();
    }

    if (!validateGenericConstraints(funcDecl->genericParams,
                                    funcDecl->genericParamConstraints,
                                    args,
                                    loc,
                                    name))
        return types::unknown();

    // Build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < funcDecl->genericParams.size(); ++i) {
        substitutions[funcDecl->genericParams[i]] = args[i];
    }
    genericFunctionSubstitutions_[mangledName] = substitutions;

    // Push substitution context and analyze function signature
    pushTypeParams(substitutions);

    // Build parameter types with substitution
    std::vector<TypeRef> paramTypes;
    for (const auto &param : funcDecl->params) {
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
    functionDeclTypes_[funcDecl] = instantiatedType;

    return instantiatedType;
}

bool Sema::pushSubstitutionContext(const std::string &mangledName) {
    auto fnSubst = genericFunctionSubstitutions_.find(mangledName);
    if (fnSubst != genericFunctionSubstitutions_.end()) {
        pushTypeParams(fnSubst->second);
        return true;
    }

    auto typeSubst = genericTypeSubstitutions_.find(mangledName);
    if (typeSubst != genericTypeSubstitutions_.end()) {
        pushTypeParams(typeSubst->second);
        return true;
    }

    // Check if this is an instantiated generic (contains $)
    size_t dollarPos = mangledName.find('$');
    if (dollarPos == std::string::npos)
        return false;

    // Extract base name and type argument names
    std::string baseName = mangledName.substr(0, dollarPos);
    std::vector<std::string> typeArgNames;
    size_t pos = dollarPos;
    while (pos != std::string::npos) {
        size_t nextDollar = mangledName.find('$', pos + 1);
        if (nextDollar == std::string::npos) {
            typeArgNames.push_back(mangledName.substr(pos + 1));
            break;
        } else {
            typeArgNames.push_back(mangledName.substr(pos + 1, nextDollar - pos - 1));
            pos = nextDollar;
        }
    }

    // Look up the generic declaration (could be a type or function)
    std::vector<std::string> genericParams;
    auto typeIt = genericTypeDecls_.find(baseName);
    if (typeIt != genericTypeDecls_.end()) {
        genericParams = getGenericParams(typeIt->second);
    } else {
        auto funcIt = genericFunctionDecls_.find(baseName);
        if (funcIt != genericFunctionDecls_.end()) {
            genericParams = funcIt->second->genericParams;
        } else {
            return false;
        }
    }

    if (typeArgNames.size() != genericParams.size())
        return false;

    // Resolve type arguments and build substitution map
    std::map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < genericParams.size(); ++i) {
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

} // namespace il::frontends::zia
