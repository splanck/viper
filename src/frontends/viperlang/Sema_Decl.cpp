//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Decl.cpp
/// @brief Declaration analysis for the ViperLang semantic analyzer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Sema.hpp"

namespace il::frontends::viperlang
{

//=============================================================================
// Declaration Analysis
//=============================================================================

void Sema::analyzeImport(ImportDecl & /*decl*/)
{
    // TODO: Implement import resolution
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

    popScope();
    currentSelfType_ = nullptr;
}

void Sema::registerEntityMembers(EntityDecl &decl)
{
    // Register field types
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

void Sema::registerValueMembers(ValueDecl &decl)
{
    // Register field types
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

    // Register method types
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

void Sema::registerInterfaceMembers(InterfaceDecl &decl)
{
    // Register method types for interface
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

void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    auto selfType = types::entity(decl.name);
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


} // namespace il::frontends::viperlang
