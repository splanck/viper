//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Decl.cpp
/// @brief Declaration analysis for the Zia semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Sema.hpp"

namespace il::frontends::zia
{

//=============================================================================
// Declaration Analysis
//=============================================================================

void Sema::analyzeBind(BindDecl &decl)
{
    if (decl.path.empty())
    {
        error(decl.loc, "Bind path cannot be empty");
        return;
    }

    binds_.insert(decl.path);

    // Extract module name from bind path
    // For "./colors" or "../utils/colors", extract "colors"
    // For "colors", use "colors"
    std::string moduleName;
    if (!decl.alias.empty())
    {
        // Use alias if provided: bind "./colors" as c;
        moduleName = decl.alias;
    }
    else
    {
        // Extract filename without extension from path
        std::string path = decl.path;
        // Remove directory components
        auto lastSlash = path.rfind('/');
        if (lastSlash != std::string::npos)
        {
            path = path.substr(lastSlash + 1);
        }
        // Remove .zia extension if present
        auto extPos = path.rfind(".zia");
        if (extPos != std::string::npos)
        {
            path = path.substr(0, extPos);
        }
        moduleName = path;
    }

    // Register the module name as a Module symbol for qualified access
    if (!moduleName.empty())
    {
        Symbol sym;
        sym.kind = Symbol::Kind::Module;
        sym.name = moduleName;
        sym.type = types::module(moduleName);
        sym.isFinal = true;
        defineSymbol(moduleName, sym);
    }
}

void Sema::analyzeGlobalVarDecl(GlobalVarDecl &decl)
{
    // Analyze initializer if present
    if (decl.initializer)
    {
        TypeRef initType = analyzeExpr(decl.initializer.get());

        // If type was inferred, update the symbol
        Symbol *sym = lookupSymbol(decl.name);
        if (sym && sym->type->isUnknown())
        {
            sym->type = initType;
        }
        else if (sym && !sym->type->isAssignableFrom(*initType))
        {
            errorTypeMismatch(decl.initializer->loc, sym->type, initType);
        }
    }
}

void Sema::analyzeValueDecl(ValueDecl &decl)
{
    auto selfType = types::value(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze fields
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            analyzeFieldDecl(*static_cast<FieldDecl *>(member.get()), selfType);
        }
    }

    // Analyze methods
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            analyzeMethodDecl(*static_cast<MethodDecl *>(member.get()), selfType);
        }
    }

    // Validate interface implementations after members are known
    for (const auto &ifaceName : decl.interfaces)
    {
        auto ifaceIt = interfaceDecls_.find(ifaceName);
        if (ifaceIt == interfaceDecls_.end())
        {
            error(decl.loc, "Unknown interface: " + ifaceName);
            continue;
        }

        bool ok = true;
        InterfaceDecl *iface = ifaceIt->second;
        for (auto &member : iface->members)
        {
            if (member->kind != DeclKind::Method)
                continue;

            auto *ifaceMethod = static_cast<MethodDecl *>(member.get());
            std::string ifaceKey = ifaceName + "." + ifaceMethod->name;
            auto ifaceTypeIt = methodTypes_.find(ifaceKey);
            if (ifaceTypeIt == methodTypes_.end())
                continue;

            std::string implKey = decl.name + "." + ifaceMethod->name;
            auto implIt = methodTypes_.find(implKey);
            if (implIt == methodTypes_.end())
            {
                error(decl.loc,
                      "Type '" + decl.name + "' does not implement interface method '" + ifaceName +
                          "." + ifaceMethod->name + "'");
                ok = false;
                continue;
            }

            if (!implIt->second->equals(*ifaceTypeIt->second))
            {
                error(decl.loc,
                      "Method '" + decl.name + "." + ifaceMethod->name +
                          "' does not match interface '" + ifaceName + "." + ifaceMethod->name +
                          "' signature");
                ok = false;
            }

            auto visIt = memberVisibility_.find(implKey);
            if (visIt != memberVisibility_.end() && visIt->second != Visibility::Public)
            {
                error(decl.loc,
                      "Method '" + decl.name + "." + ifaceMethod->name +
                          "' must be public to satisfy interface '" + ifaceName + "'");
                ok = false;
            }
        }

        if (ok)
            types::registerInterfaceImplementation(decl.name, ifaceName);
    }

    popScope();
    currentSelfType_ = nullptr;
}

template <typename T>
void Sema::registerTypeMembers(T &decl, bool includeFields)
{
    // Register field types (if applicable)
    if (includeFields)
    {
        for (auto &member : decl.members)
        {
            if (member->kind == DeclKind::Field)
            {
                auto *field = static_cast<FieldDecl *>(member.get());
                TypeRef fieldType = field->type ? resolveTypeNode(field->type.get()) : types::unknown();
                std::string fieldKey = decl.name + "." + field->name;
                fieldTypes_[fieldKey] = fieldType;
                memberVisibility_[fieldKey] = field->visibility;
            }
        }
    }

    // Register method types (signatures only, not bodies)
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            TypeRef returnType =
                method->returnType ? resolveTypeNode(method->returnType.get()) : types::voidType();
            std::vector<TypeRef> paramTypes;
            for (const auto &param : method->params)
            {
                TypeRef paramType =
                    param.type ? resolveTypeNode(param.type.get()) : types::unknown();
                paramTypes.push_back(paramType);
            }
            std::string methodKey = decl.name + "." + method->name;
            methodTypes_[methodKey] = types::function(paramTypes, returnType);
            memberVisibility_[methodKey] = method->visibility;
        }
    }
}

// Explicit template instantiations
template void Sema::registerTypeMembers<EntityDecl>(EntityDecl &, bool);
template void Sema::registerTypeMembers<ValueDecl>(ValueDecl &, bool);
template void Sema::registerTypeMembers<InterfaceDecl>(InterfaceDecl &, bool);

void Sema::registerEntityMembers(EntityDecl &decl)
{
    registerTypeMembers(decl, true);
}

void Sema::registerValueMembers(ValueDecl &decl)
{
    registerTypeMembers(decl, true);
}

void Sema::registerInterfaceMembers(InterfaceDecl &decl)
{
    registerTypeMembers(decl, false);
}

void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    auto selfType = types::entity(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // BUG-VL-006 fix: Handle inheritance - add parent's members to scope
    if (!decl.baseClass.empty())
    {
        auto parentIt = entityDecls_.find(decl.baseClass);
        if (parentIt == entityDecls_.end())
        {
            error(decl.loc, "Unknown base class: " + decl.baseClass);
        }
        else
        {
            EntityDecl *parent = parentIt->second;
            // BUG-VL-007 fix: Register inheritance for polymorphism support
            types::registerEntityInheritance(decl.name, parent->name);
            // Add parent's fields to this entity's scope
            for (auto &member : parent->members)
            {
                if (member->kind == DeclKind::Field)
                {
                    auto *field = static_cast<FieldDecl *>(member.get());
                    std::string fieldKey = parent->name + "." + field->name;
                    auto typeIt = fieldTypes_.find(fieldKey);
                    if (typeIt != fieldTypes_.end())
                    {
                        Symbol sym;
                        sym.kind = Symbol::Kind::Field;
                        sym.name = field->name;
                        sym.type = typeIt->second;
                        defineSymbol(field->name, sym);
                        // Also register in this entity's field types
                        fieldTypes_[decl.name + "." + field->name] = typeIt->second;
                    }
                }
            }
            // Add parent's methods to this entity's scope
            for (auto &member : parent->members)
            {
                if (member->kind == DeclKind::Method)
                {
                    auto *method = static_cast<MethodDecl *>(member.get());
                    std::string methodKey = parent->name + "." + method->name;
                    auto typeIt = methodTypes_.find(methodKey);
                    if (typeIt != methodTypes_.end())
                    {
                        Symbol sym;
                        sym.kind = Symbol::Kind::Method;
                        sym.name = method->name;
                        sym.type = typeIt->second;
                        sym.isFinal = true;
                        sym.decl = method;
                        defineSymbol(method->name, sym);
                    }
                }
            }
        }
    }

    // Analyze fields first (adds them to scope)
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Field)
        {
            analyzeFieldDecl(*static_cast<FieldDecl *>(member.get()), selfType);
        }
    }

    // Pre-define method symbols in scope so they can be called without 'self.'
    // This allows methods to call each other by bare name within the entity.
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            TypeRef returnType =
                method->returnType ? resolveTypeNode(method->returnType.get()) : types::voidType();
            std::vector<TypeRef> paramTypes;
            for (const auto &param : method->params)
            {
                TypeRef paramType =
                    param.type ? resolveTypeNode(param.type.get()) : types::unknown();
                paramTypes.push_back(paramType);
            }
            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = method->name;
            sym.type = types::function(paramTypes, returnType);
            sym.isFinal = true;
            sym.decl = method;
            defineSymbol(method->name, sym);
        }
    }

    // Analyze methods (now they can reference each other by bare name)
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            analyzeMethodDecl(*static_cast<MethodDecl *>(member.get()), selfType);
        }
    }

    for (const auto &ifaceName : decl.interfaces)
    {
        auto ifaceIt = interfaceDecls_.find(ifaceName);
        if (ifaceIt == interfaceDecls_.end())
        {
            error(decl.loc, "Unknown interface: " + ifaceName);
            continue;
        }

        bool ok = true;
        InterfaceDecl *iface = ifaceIt->second;
        for (auto &member : iface->members)
        {
            if (member->kind != DeclKind::Method)
                continue;

            auto *ifaceMethod = static_cast<MethodDecl *>(member.get());
            std::string ifaceKey = ifaceName + "." + ifaceMethod->name;
            auto ifaceTypeIt = methodTypes_.find(ifaceKey);
            if (ifaceTypeIt == methodTypes_.end())
                continue;

            std::string implKey = decl.name + "." + ifaceMethod->name;
            auto implIt = methodTypes_.find(implKey);
            if (implIt == methodTypes_.end())
            {
                error(decl.loc,
                      "Type '" + decl.name + "' does not implement interface method '" + ifaceName +
                          "." + ifaceMethod->name + "'");
                ok = false;
                continue;
            }

            if (!implIt->second->equals(*ifaceTypeIt->second))
            {
                error(decl.loc,
                      "Method '" + decl.name + "." + ifaceMethod->name +
                          "' does not match interface '" + ifaceName + "." + ifaceMethod->name +
                          "' signature");
                ok = false;
            }

            auto visIt = memberVisibility_.find(implKey);
            if (visIt != memberVisibility_.end() && visIt->second != Visibility::Public)
            {
                error(decl.loc,
                      "Method '" + decl.name + "." + ifaceMethod->name +
                          "' must be public to satisfy interface '" + ifaceName + "'");
                ok = false;
            }
        }

        if (ok)
            types::registerInterfaceImplementation(decl.name, ifaceName);
    }

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::analyzeInterfaceDecl(InterfaceDecl &decl)
{
    auto selfType = types::interface(decl.name);
    currentSelfType_ = selfType;

    pushScope();

    // Analyze method signatures
    for (auto &member : decl.members)
    {
        if (member->kind == DeclKind::Method)
        {
            auto *method = static_cast<MethodDecl *>(member.get());
            // Just register the method signature, no body analysis
            TypeRef returnType =
                method->returnType ? resolveTypeNode(method->returnType.get()) : types::voidType();
            std::vector<TypeRef> paramTypes;
            for (const auto &param : method->params)
            {
                paramTypes.push_back(param.type ? resolveTypeNode(param.type.get())
                                                : types::unknown());
            }
            auto methodType = types::function(paramTypes, returnType);

            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = method->name;
            sym.type = methodType;
            sym.decl = method;
            defineSymbol(method->name, sym);
        }
    }

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::analyzeFunctionDecl(FunctionDecl &decl)
{
    currentFunction_ = &decl;
    expectedReturnType_ =
        decl.returnType ? resolveTypeNode(decl.returnType.get()) : types::voidType();

    pushScope();

    // Define parameters
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true; // Parameters are immutable by default
        defineSymbol(param.name, sym);
    }

    // Analyze body
    if (decl.body)
    {
        analyzeStmt(decl.body.get());
    }

    popScope();

    currentFunction_ = nullptr;
    expectedReturnType_ = nullptr;
}

void Sema::analyzeFieldDecl(FieldDecl &decl, TypeRef ownerType)
{
    TypeRef fieldType = decl.type ? resolveTypeNode(decl.type.get()) : types::unknown();

    // Check initializer type
    if (decl.initializer)
    {
        TypeRef initType = analyzeExpr(decl.initializer.get());
        if (!fieldType->isAssignableFrom(*initType))
        {
            errorTypeMismatch(decl.initializer->loc, fieldType, initType);
        }
    }

    // Store field type and visibility for access checking
    if (ownerType)
    {
        std::string fieldKey = ownerType->name + "." + decl.name;
        fieldTypes_[fieldKey] = fieldType;
        memberVisibility_[fieldKey] = decl.visibility;
    }

    Symbol sym;
    sym.kind = Symbol::Kind::Field;
    sym.name = decl.name;
    sym.type = fieldType;
    sym.isFinal = decl.isFinal;
    sym.decl = &decl;
    defineSymbol(decl.name, sym);
}

void Sema::analyzeMethodDecl(MethodDecl &decl, TypeRef ownerType)
{
    currentSelfType_ = ownerType;
    TypeRef returnType =
        decl.returnType ? resolveTypeNode(decl.returnType.get()) : types::voidType();
    expectedReturnType_ = returnType;

    // Build parameter types
    std::vector<TypeRef> paramTypes;
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();
        paramTypes.push_back(paramType);
    }

    // Register method type: "TypeName.methodName" -> function type
    std::string methodKey = ownerType->name + "." + decl.name;
    methodTypes_[methodKey] = types::function(paramTypes, returnType);
    memberVisibility_[methodKey] = decl.visibility;

    pushScope();

    // Define 'self' parameter implicitly
    Symbol selfSym;
    selfSym.kind = Symbol::Kind::Parameter;
    selfSym.name = "self";
    selfSym.type = ownerType;
    selfSym.isFinal = true;
    defineSymbol("self", selfSym);

    // Define explicit parameters
    for (const auto &param : decl.params)
    {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym);
    }

    // Analyze body
    if (decl.body)
    {
        analyzeStmt(decl.body.get());
    }

    popScope();

    expectedReturnType_ = nullptr;
}


} // namespace il::frontends::zia
