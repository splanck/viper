//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Decl.cpp
// Purpose: Declaration collection for Viper Pascal.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive comparison
using common::char_utils::toLowercase;

// Alias for compatibility with existing code
inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Declaration Collection (Pass 1)
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::collectDeclarations(Program &prog)
{
    for (auto &decl : prog.decls)
    {
        if (decl)
            collectDecl(*decl);
    }
}

void SemanticAnalyzer::collectDeclarations(Unit &unit)
{
    // Interface declarations - check for illegal declarations
    for (auto &decl : unit.interfaceDecls)
    {
        if (decl)
        {
            // Variables cannot be exported from unit interface
            if (decl->kind == DeclKind::Var)
            {
                error(decl->loc, "variables cannot be exported from unit interface");
            }
            // Procedure/function implementations cannot appear in interface
            else if (decl->kind == DeclKind::Procedure)
            {
                auto &pd = static_cast<ProcedureDecl &>(*decl);
                if (pd.body)
                {
                    error(decl->loc,
                          "procedure implementation cannot appear in unit interface section; "
                          "move the body to the implementation section");
                }
            }
            else if (decl->kind == DeclKind::Function)
            {
                auto &fd = static_cast<FunctionDecl &>(*decl);
                if (fd.body)
                {
                    error(decl->loc,
                          "function implementation cannot appear in unit interface section; "
                          "move the body to the implementation section");
                }
            }
            // Constructor/destructor implementations cannot appear in interface
            else if (decl->kind == DeclKind::Constructor)
            {
                auto &cd = static_cast<ConstructorDecl &>(*decl);
                if (cd.body)
                {
                    error(decl->loc,
                          "constructor implementation cannot appear in unit interface section; "
                          "move the body to the implementation section");
                }
            }
            else if (decl->kind == DeclKind::Destructor)
            {
                auto &dd = static_cast<DestructorDecl &>(*decl);
                if (dd.body)
                {
                    error(decl->loc,
                          "destructor implementation cannot appear in unit interface section; "
                          "move the body to the implementation section");
                }
            }
            collectDecl(*decl);
        }
    }
    // Implementation declarations
    for (auto &decl : unit.implDecls)
    {
        if (decl)
            collectDecl(*decl);
    }
}

void SemanticAnalyzer::collectDecl(Decl &decl)
{
    switch (decl.kind)
    {
        case DeclKind::Type:
        {
            auto &td = static_cast<TypeDecl &>(decl);
            registerType(td.name, *td.type);
            break;
        }
        case DeclKind::Var:
        {
            auto &vd = static_cast<VarDecl &>(decl);
            for (const auto &name : vd.names)
            {
                registerVariable(name, *vd.type);
            }
            break;
        }
        case DeclKind::Const:
        {
            auto &cd = static_cast<ConstDecl &>(decl);
            registerConstant(cd.name, *cd.value, cd.type.get());
            break;
        }
        case DeclKind::Procedure:
        {
            auto &pd = static_cast<ProcedureDecl &>(decl);
            registerProcedure(pd);
            break;
        }
        case DeclKind::Function:
        {
            auto &fd = static_cast<FunctionDecl &>(decl);
            registerFunction(fd);
            break;
        }
        case DeclKind::Class:
        {
            auto &cd = static_cast<ClassDecl &>(decl);
            registerClass(cd);
            break;
        }
        case DeclKind::Interface:
        {
            auto &id = static_cast<InterfaceDecl &>(decl);
            registerInterface(id);
            break;
        }
        default:
            // Other declarations (constructor, destructor, etc.) to be handled later
            break;
    }
}

void SemanticAnalyzer::registerType(const std::string &name, TypeNode &typeNode)
{
    std::string key = toLower(name);
    PasType resolved = resolveType(typeNode);
    resolved.name = name;
    types_[key] = resolved;

    // For enum types, register each member as a constant
    if (resolved.kind == PasTypeKind::Enum)
    {
        for (size_t i = 0; i < resolved.enumValues.size(); ++i)
        {
            std::string constKey = toLower(resolved.enumValues[i]);
            // Check for duplicate constant name
            if (constants_.find(constKey) != constants_.end())
            {
                error(typeNode.loc,
                      "enum constant '" + resolved.enumValues[i] + "' is already defined");
                continue;
            }
            constants_[constKey] =
                PasType::enumConstant(name, resolved.enumValues, static_cast<int>(i));
        }
    }
}

void SemanticAnalyzer::registerVariable(const std::string &name, TypeNode &typeNode)
{
    std::string key = toLower(name);
    PasType resolved = resolveType(typeNode);

    // Local variables (inside routines) need definite assignment tracking
    if (routineDepth_ > 0)
    {
        addLocalVariable(key, resolved);
    }
    else
    {
        addVariable(key, resolved);
    }
}

void SemanticAnalyzer::registerConstant(const std::string &name, Expr &value, TypeNode *typeNode)
{
    std::string key = toLower(name);
    PasType type;

    if (typeNode)
    {
        type = resolveType(*typeNode);
    }
    else
    {
        // Infer type from value
        type = typeOf(value);
    }

    // Check that the value is a compile-time constant
    if (!isConstantExpr(value))
    {
        error(value.loc, "constant expression required");
        constants_[key] = type;
        return;
    }

    // Check for division by zero in the constant expression
    if (checkConstantDivZero(value))
    {
        constants_[key] = type;
        return;
    }

    constants_[key] = type;

    // Fold constant expression and store value
    switch (type.kind)
    {
        case PasTypeKind::Integer:
        {
            int64_t val = evaluateConstantInt(value);
            constantValues_[key] = val;
            break;
        }
        case PasTypeKind::Real:
        {
            double val = evaluateConstantReal(value);
            constantRealValues_[key] = val;
            break;
        }
        case PasTypeKind::String:
        {
            std::string val = evaluateConstantString(value);
            constantStrValues_[key] = val;
            break;
        }
        case PasTypeKind::Boolean:
        {
            bool val = evaluateConstantBool(value);
            // Store as integer (0/1) for consistency with other constant lookups
            constantValues_[key] = val ? 1 : 0;
            break;
        }
        default:
            // For other types (enum, etc.), don't store a computed value
            break;
    }
}

void SemanticAnalyzer::registerProcedure(ProcedureDecl &decl)
{
    // Method implementations (e.g., TClass.Method) use qualified names
    // and don't participate in free function overloading
    std::string key =
        decl.isMethod() ? toLower(decl.className + "." + decl.name) : toLower(decl.name);

    // v0.1: Check for user-defined overloading (not allowed for free procedures)
    if (!decl.isMethod())
    {
        auto existingIt = functions_.find(key);
        if (existingIt != functions_.end() && !existingIt->second.isForward && !decl.isForward)
        {
            error(decl.loc,
                  "procedure '" + decl.name +
                      "' is already defined; "
                      "function/procedure overloading is not supported in Viper Pascal v0.1");
            return;
        }
    }

    // Validate default parameters
    size_t requiredParams = validateDefaultParams(decl.params, decl.loc);

    FuncSignature sig;
    sig.name = decl.name;
    sig.returnType = PasType::voidType();
    sig.isForward = decl.isForward;
    sig.requiredParams = requiredParams;

    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        const auto &param = decl.params[i];
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        sig.params.emplace_back(param.name, paramType);
        sig.isVarParam.push_back(param.isVar);
        sig.hasDefault.push_back(param.defaultValue != nullptr);

        // Store default expression for lowering
        if (param.defaultValue)
        {
            std::string defKey = key + ":" + std::to_string(i);
            defaultParamExprs_[defKey] = param.defaultValue.get();
        }
    }

    functions_[key] = sig;
}

void SemanticAnalyzer::registerFunction(FunctionDecl &decl)
{
    // Method implementations (e.g., TClass.Method) use qualified names
    // and don't participate in free function overloading
    std::string key =
        decl.isMethod() ? toLower(decl.className + "." + decl.name) : toLower(decl.name);

    // v0.1: Check for user-defined overloading (not allowed for free functions)
    if (!decl.isMethod())
    {
        auto existingIt = functions_.find(key);
        if (existingIt != functions_.end() && !existingIt->second.isForward && !decl.isForward)
        {
            error(decl.loc,
                  "function '" + decl.name +
                      "' is already defined; "
                      "function/procedure overloading is not supported in Viper Pascal v0.1");
            return;
        }
    }

    // Validate default parameters
    size_t requiredParams = validateDefaultParams(decl.params, decl.loc);

    FuncSignature sig;
    sig.name = decl.name;
    sig.returnType = decl.returnType ? resolveType(*decl.returnType) : PasType::unknown();
    sig.isForward = decl.isForward;
    sig.requiredParams = requiredParams;

    for (size_t i = 0; i < decl.params.size(); ++i)
    {
        const auto &param = decl.params[i];
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        sig.params.emplace_back(param.name, paramType);
        sig.isVarParam.push_back(param.isVar);
        sig.hasDefault.push_back(param.defaultValue != nullptr);

        // Store default expression for lowering
        if (param.defaultValue)
        {
            std::string defKey = key + ":" + std::to_string(i);
            defaultParamExprs_[defKey] = param.defaultValue.get();
        }
    }

    functions_[key] = sig;
}

void SemanticAnalyzer::registerClass(ClassDecl &decl)
{
    std::string key = toLower(decl.name);

    // Prevent redefinition of built-in Exception class
    if (key == "exception")
    {
        error(decl.loc, "cannot redefine built-in class 'Exception'");
        return;
    }

    // Register as a type
    PasType classType = PasType::classType(decl.name);
    types_[key] = classType;

    // Build ClassInfo
    ClassInfo info;
    info.name = decl.name;
    info.baseClass = decl.baseClass;
    info.interfaces = decl.interfaces;
    info.loc = decl.loc;

    // First pass: fields/methods/ctors/dtors
    for (const auto &member : decl.members)
    {
        switch (member.memberKind)
        {
            case ClassMember::Kind::Field:
            {
                FieldInfo field;
                field.name = member.fieldName;
                field.type = member.fieldType ? resolveType(*member.fieldType) : PasType::unknown();
                field.isWeak = member.isWeak;
                field.visibility = member.visibility;
                field.loc = member.loc;
                info.fields[toLower(member.fieldName)] = field;
                break;
            }
            case ClassMember::Kind::Method:
            {
                if (member.methodDecl)
                {
                    if (member.methodDecl->kind == DeclKind::Function)
                    {
                        auto &fd = static_cast<FunctionDecl &>(*member.methodDecl);
                        MethodInfo method;
                        method.name = fd.name;
                        method.returnType =
                            fd.returnType ? resolveType(*fd.returnType) : PasType::unknown();
                        method.isVirtual = fd.isVirtual;
                        method.isOverride = fd.isOverride;
                        method.isAbstract = fd.isAbstract;
                        method.visibility = member.visibility;
                        method.loc = fd.loc;
                        size_t required = 0;
                        std::string methodKey = toLower(decl.name) + "." + toLower(fd.name);
                        for (size_t i = 0; i < fd.params.size(); ++i)
                        {
                            const auto &param = fd.params[i];
                            PasType paramType =
                                param.type ? resolveType(*param.type) : PasType::unknown();
                            method.params.emplace_back(param.name, paramType);
                            method.isVarParam.push_back(param.isVar);
                            method.hasDefault.push_back(param.defaultValue != nullptr);
                            if (!param.defaultValue)
                                ++required;
                            else
                            {
                                // Store default expression for lowering
                                std::string defKey = methodKey + ":" + std::to_string(i);
                                defaultParamExprs_[defKey] = param.defaultValue.get();
                            }
                        }
                        method.requiredParams = required;
                        // Check for duplicate signature in existing overloads
                        std::string key = toLower(fd.name);
                        auto &overloads = info.methods[key];
                        bool hasDuplicate = false;
                        for (const auto &existing : overloads)
                        {
                            if (parameterTypesMatch(existing, method))
                            {
                                error(fd.loc,
                                      "duplicate method '" + fd.name +
                                          "' with same parameter signature");
                                hasDuplicate = true;
                                break;
                            }
                        }
                        if (!hasDuplicate)
                            overloads.push_back(method);
                    }
                    else if (member.methodDecl->kind == DeclKind::Procedure)
                    {
                        auto &pd = static_cast<ProcedureDecl &>(*member.methodDecl);
                        MethodInfo method;
                        method.name = pd.name;
                        method.returnType = PasType::voidType();
                        method.isVirtual = pd.isVirtual;
                        method.isOverride = pd.isOverride;
                        method.isAbstract = pd.isAbstract;
                        method.visibility = member.visibility;
                        method.loc = pd.loc;
                        size_t required = 0;
                        std::string methodKey = toLower(decl.name) + "." + toLower(pd.name);
                        for (size_t i = 0; i < pd.params.size(); ++i)
                        {
                            const auto &param = pd.params[i];
                            PasType paramType =
                                param.type ? resolveType(*param.type) : PasType::unknown();
                            method.params.emplace_back(param.name, paramType);
                            method.isVarParam.push_back(param.isVar);
                            method.hasDefault.push_back(param.defaultValue != nullptr);
                            if (!param.defaultValue)
                                ++required;
                            else
                            {
                                // Store default expression for lowering
                                std::string defKey = methodKey + ":" + std::to_string(i);
                                defaultParamExprs_[defKey] = param.defaultValue.get();
                            }
                        }
                        method.requiredParams = required;
                        // Check for duplicate signature in existing overloads
                        std::string key = toLower(pd.name);
                        auto &overloads = info.methods[key];
                        bool hasDuplicate = false;
                        for (const auto &existing : overloads)
                        {
                            if (parameterTypesMatch(existing, method))
                            {
                                error(pd.loc,
                                      "duplicate method '" + pd.name +
                                          "' with same parameter signature");
                                hasDuplicate = true;
                                break;
                            }
                        }
                        if (!hasDuplicate)
                            overloads.push_back(method);
                    }
                }
                break;
            }
            case ClassMember::Kind::Constructor:
            {
                info.hasConstructor = true;
                if (member.methodDecl && member.methodDecl->kind == DeclKind::Constructor)
                {
                    auto &cd = static_cast<ConstructorDecl &>(*member.methodDecl);
                    MethodInfo method;
                    method.name = cd.name;
                    method.returnType = PasType::voidType();
                    method.visibility = member.visibility;
                    method.loc = cd.loc;
                    size_t required = 0;
                    std::string methodKey = toLower(decl.name) + "." + toLower(cd.name);
                    for (size_t i = 0; i < cd.params.size(); ++i)
                    {
                        const auto &param = cd.params[i];
                        PasType paramType =
                            param.type ? resolveType(*param.type) : PasType::unknown();
                        method.params.emplace_back(param.name, paramType);
                        method.isVarParam.push_back(param.isVar);
                        method.hasDefault.push_back(param.defaultValue != nullptr);
                        if (!param.defaultValue)
                            ++required;
                        else
                        {
                            // Store default expression for lowering
                            std::string defKey = methodKey + ":" + std::to_string(i);
                            defaultParamExprs_[defKey] = param.defaultValue.get();
                        }
                    }
                    method.requiredParams = required;
                    // Check for duplicate signature in existing overloads
                    std::string key = toLower(cd.name);
                    auto &overloads = info.methods[key];
                    bool hasDuplicate = false;
                    for (const auto &existing : overloads)
                    {
                        if (parameterTypesMatch(existing, method))
                        {
                            error(cd.loc, "duplicate constructor with same parameter signature");
                            hasDuplicate = true;
                            break;
                        }
                    }
                    if (!hasDuplicate)
                        overloads.push_back(method);
                }
                break;
            }
            case ClassMember::Kind::Destructor:
            {
                info.hasDestructor = true;
                if (member.methodDecl && member.methodDecl->kind == DeclKind::Destructor)
                {
                    auto &dd = static_cast<DestructorDecl &>(*member.methodDecl);
                    MethodInfo method;
                    method.name = dd.name;
                    method.returnType = PasType::voidType();
                    // Destructors are implicitly virtual per spec
                    method.isVirtual = true;
                    // If base class has a destructor, this is an override
                    if (!decl.baseClass.empty())
                    {
                        const ClassInfo *baseInfo = lookupClass(toLower(decl.baseClass));
                        if (baseInfo && baseInfo->hasDestructor)
                        {
                            method.isOverride = true;
                        }
                    }
                    method.visibility = member.visibility;
                    method.loc = dd.loc;
                    // Destructors cannot be overloaded
                    info.methods[toLower(dd.name)].push_back(method);
                }
                break;
            }
            default:
                break;
        }
    }

    // Second pass: properties (validate and record)
    for (const auto &member : decl.members)
    {
        if (member.memberKind != ClassMember::Kind::Property || !member.property)
            continue;

        const PropertyDecl &pd = *member.property;
        PropertyInfo pinfo;
        pinfo.name = pd.name;
        pinfo.type = pd.type ? resolveType(*pd.type) : PasType::unknown();
        pinfo.visibility = member.visibility;
        pinfo.loc = pd.loc;

        auto lower = [&](const std::string &n) { return toLower(n); };

        // Validate getter target
        if (pd.getter.empty())
        {
            error(pd.loc, "property '" + pd.name + "' is missing required read accessor");
        }
        else
        {
            std::string key = lower(pd.getter);
            auto fit = info.fields.find(key);
            if (fit != info.fields.end())
            {
                // Field-backed getter
                if (!fit->second.type.isError() && !isAssignableFrom(pinfo.type, fit->second.type))
                {
                    error(pd.loc,
                          "getter field '" + pd.getter + "' type mismatch for property '" +
                              pd.name + "'");
                }
                pinfo.getter.kind = PropertyAccessor::Kind::Field;
                pinfo.getter.name = pd.getter;
            }
            else
            {
                const MethodInfo *m = info.findMethod(key);
                if (!m)
                {
                    error(pd.loc,
                          "undefined getter '" + pd.getter + "' for property '" + pd.name + "'");
                }
                else
                {
                    if (m->requiredParams != 0)
                    {
                        error(pd.loc, "getter '" + pd.getter + "' must have no parameters");
                    }
                    if (!m->returnType.isError() && !isAssignableFrom(pinfo.type, m->returnType))
                    {
                        error(pd.loc,
                              "getter '" + pd.getter + "' return type mismatch for property '" +
                                  pd.name + "'");
                    }
                    pinfo.getter.kind = PropertyAccessor::Kind::Method;
                    pinfo.getter.name = pd.getter;
                }
            }
        }

        // Validate setter target (optional)
        if (!pd.setter.empty())
        {
            std::string key = lower(pd.setter);
            auto fit = info.fields.find(key);
            if (fit != info.fields.end())
            {
                if (!fit->second.type.isError() && !isAssignableFrom(fit->second.type, pinfo.type))
                {
                    error(pd.loc,
                          "setter field '" + pd.setter + "' type mismatch for property '" +
                              pd.name + "'");
                }
                pinfo.setter.kind = PropertyAccessor::Kind::Field;
                pinfo.setter.name = pd.setter;
            }
            else
            {
                const MethodInfo *m = info.findMethod(key);
                if (!m)
                {
                    error(pd.loc,
                          "undefined setter '" + pd.setter + "' for property '" + pd.name + "'");
                }
                else
                {
                    if (m->returnType.kind != PasTypeKind::Void)
                    {
                        error(pd.loc, "setter '" + pd.setter + "' must be a procedure");
                    }
                    if (m->params.size() != 1)
                    {
                        error(pd.loc, "setter '" + pd.setter + "' must have exactly one parameter");
                    }
                    else
                    {
                        const PasType &pt = m->params[0].second;
                        if (!pt.isError() && !isAssignableFrom(pt, pinfo.type))
                        {
                            error(pd.loc,
                                  "setter '" + pd.setter +
                                      "' parameter type mismatch for property '" + pd.name + "'");
                        }
                    }
                    pinfo.setter.kind = PropertyAccessor::Kind::Method;
                    pinfo.setter.name = pd.setter;
                }
            }
        }

        info.properties[toLower(pinfo.name)] = pinfo;
    }

    // Validate interface implementations
    for (const std::string &ifaceName : decl.interfaces)
    {
        std::string ifaceKey = toLower(ifaceName);
        const InterfaceInfo *iface = lookupInterface(ifaceKey);
        if (!iface)
        {
            error(decl.loc, "unknown interface '" + ifaceName + "'");
            continue;
        }

        // Check each interface method is implemented (including inherited methods)
        for (const auto &[methodName, ifaceMethods] : iface->methods)
        {
            std::string methodKey = toLower(methodName);

            // Search for method in class and its base classes
            const std::vector<MethodInfo> *classMethods = nullptr;

            // First check current class
            auto it = info.methods.find(methodKey);
            if (it != info.methods.end())
            {
                classMethods = &it->second;
            }
            else
            {
                // Walk up inheritance chain to find the method
                std::string baseClassName = decl.baseClass;
                while (!baseClassName.empty() && !classMethods)
                {
                    const ClassInfo *baseClass = lookupClass(toLower(baseClassName));
                    if (!baseClass)
                        break;

                    auto baseIt = baseClass->methods.find(methodKey);
                    if (baseIt != baseClass->methods.end())
                    {
                        classMethods = &baseIt->second;
                    }
                    else
                    {
                        baseClassName = baseClass->baseClass;
                    }
                }
            }

            if (!classMethods)
            {
                error(decl.loc,
                      "class '" + decl.name + "' does not implement method '" + methodName +
                          "' required by interface '" + ifaceName + "'");
                continue;
            }

            // For each interface method overload, find a compatible class method
            for (const MethodInfo &ifaceMethod : ifaceMethods)
            {
                bool foundCompatible = false;
                for (const MethodInfo &classMethod : *classMethods)
                {
                    if (areSignaturesCompatible(classMethod, ifaceMethod))
                    {
                        foundCompatible = true;
                        break;
                    }
                }

                if (!foundCompatible)
                {
                    error(decl.loc,
                          "method '" + methodName + "' in class '" + decl.name +
                              "' has incompatible signature with interface '" + ifaceName + "'");
                }
            }
        }
    }

    classes_[key] = info;

    // Check constructor/destructor validity immediately
    checkConstructorDestructor(decl);
}

void SemanticAnalyzer::registerInterface(InterfaceDecl &decl)
{
    std::string key = toLower(decl.name);

    // Register as a type
    PasType ifaceType = PasType::interfaceType(decl.name);
    types_[key] = ifaceType;

    // Build InterfaceInfo
    InterfaceInfo info;
    info.name = decl.name;
    info.baseInterfaces = decl.baseInterfaces;
    info.loc = decl.loc;

    // Process method signatures
    for (const auto &sig : decl.methods)
    {
        MethodInfo method;
        method.name = sig.name;
        method.returnType = sig.returnType ? resolveType(*sig.returnType) : PasType::voidType();
        method.isVirtual = sig.isVirtual;
        method.isAbstract = sig.isAbstract;
        method.loc = sig.loc;
        for (const auto &param : sig.params)
        {
            PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
            method.params.emplace_back(param.name, paramType);
            method.isVarParam.push_back(param.isVar);
        }
        // Check for duplicate signature in existing overloads
        std::string methodKey = toLower(sig.name);
        auto &overloads = info.methods[methodKey];
        bool hasDuplicate = false;
        for (const auto &existing : overloads)
        {
            if (parameterTypesMatch(existing, method))
            {
                error(sig.loc,
                      "duplicate interface method '" + sig.name +
                          "' with same parameter signature");
                hasDuplicate = true;
                break;
            }
        }
        if (!hasDuplicate)
            overloads.push_back(method);
    }

    interfaces_[key] = info;
}

void SemanticAnalyzer::checkConstructorDestructor(ClassDecl &decl)
{
    for (const auto &member : decl.members)
    {
        if (member.memberKind == ClassMember::Kind::Destructor)
        {
            if (member.methodDecl && member.methodDecl->kind == DeclKind::Destructor)
            {
                auto &dtor = static_cast<DestructorDecl &>(*member.methodDecl);

                // Destructor must be named "Destroy"
                if (toLower(dtor.name) != "destroy")
                {
                    error(dtor.loc, "destructor must be named 'Destroy', not '" + dtor.name + "'");
                }
            }
        }
        else if (member.memberKind == ClassMember::Kind::Constructor)
        {
            if (member.methodDecl && member.methodDecl->kind == DeclKind::Constructor)
            {
                auto &ctor = static_cast<ConstructorDecl &>(*member.methodDecl);
                // Note: Return type on constructor is handled by parser (constructors don't have
                // return type) If we want to check, the AST would need to capture any erroneous
                // return type
                (void)ctor;
            }
        }
    }
}


} // namespace il::frontends::pascal
