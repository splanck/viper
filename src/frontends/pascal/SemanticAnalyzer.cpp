//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer.cpp
// Purpose: Implements semantic analysis for Viper Pascal.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

namespace
{

/// @brief Convert string to lowercase for case-insensitive comparison.
std::string toLower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

//===----------------------------------------------------------------------===//
// PasType Implementation
//===----------------------------------------------------------------------===//

std::string PasType::toString() const
{
    switch (kind)
    {
    case PasTypeKind::Integer:
        return "Integer";
    case PasTypeKind::Real:
        return "Real";
    case PasTypeKind::Boolean:
        return "Boolean";
    case PasTypeKind::String:
        return "String";
    case PasTypeKind::Enum:
        return "enum";
    case PasTypeKind::Array:
        if (elementType)
        {
            if (dimensions == 0)
                return "array of " + elementType->toString();
            return "array[" + std::to_string(dimensions) + "] of " + elementType->toString();
        }
        return "array";
    case PasTypeKind::Record:
        return "record";
    case PasTypeKind::Class:
        return name.empty() ? "class" : name;
    case PasTypeKind::Interface:
        return name.empty() ? "interface" : name;
    case PasTypeKind::Optional:
        if (innerType)
            return innerType->toString() + "?";
        return "optional";
    case PasTypeKind::Pointer:
        if (pointeeType)
            return "^" + pointeeType->toString();
        return "pointer";
    case PasTypeKind::Procedure:
        return "procedure";
    case PasTypeKind::Function:
        return "function";
    case PasTypeKind::Set:
        return "set";
    case PasTypeKind::Range:
        return "range";
    case PasTypeKind::Nil:
        return "nil";
    case PasTypeKind::Unknown:
        return "<unknown>";
    case PasTypeKind::Void:
        return "void";
    }
    return "<invalid>";
}

//===----------------------------------------------------------------------===//
// SemanticAnalyzer Constructor
//===----------------------------------------------------------------------===//

SemanticAnalyzer::SemanticAnalyzer(il::support::DiagnosticEngine &diag) : diag_(diag)
{
    registerPrimitives();
    registerBuiltins();
    // Start with global scope
    pushScope();
}

//===----------------------------------------------------------------------===//
// Public Analysis Entry Points
//===----------------------------------------------------------------------===//

bool SemanticAnalyzer::analyze(Program &prog)
{
    // Import symbols from used units
    if (!prog.usedUnits.empty())
    {
        importUnits(prog.usedUnits);
    }

    // Pass 1: Collect declarations
    collectDeclarations(prog);

    // Check class/interface semantics after all declarations are collected
    checkClassSemantics();

    // Pass 2: Analyze bodies
    analyzeBodies(prog);

    return !hasError_;
}

bool SemanticAnalyzer::analyze(Unit &unit)
{
    // Import symbols from interface-level uses
    if (!unit.usedUnits.empty())
    {
        importUnits(unit.usedUnits);
    }

    // Pass 1: Collect declarations (interface + implementation)
    collectDeclarations(unit);

    // Import symbols from implementation-level uses (after interface decls)
    if (!unit.implUsedUnits.empty())
    {
        importUnits(unit.implUsedUnits);
    }

    // Check class/interface semantics after all declarations are collected
    checkClassSemantics();

    // Pass 2: Analyze bodies
    analyzeBodies(unit);

    // Extract and register this unit's exports for other units to use
    UnitInfo exports = extractUnitExports(unit);
    registerUnit(exports);

    return !hasError_;
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
    case DeclKind::Type: {
        auto &td = static_cast<TypeDecl &>(decl);
        registerType(td.name, *td.type);
        break;
    }
    case DeclKind::Var: {
        auto &vd = static_cast<VarDecl &>(decl);
        for (const auto &name : vd.names)
        {
            registerVariable(name, *vd.type);
        }
        break;
    }
    case DeclKind::Const: {
        auto &cd = static_cast<ConstDecl &>(decl);
        registerConstant(cd.name, *cd.value, cd.type.get());
        break;
    }
    case DeclKind::Procedure: {
        auto &pd = static_cast<ProcedureDecl &>(decl);
        registerProcedure(pd);
        break;
    }
    case DeclKind::Function: {
        auto &fd = static_cast<FunctionDecl &>(decl);
        registerFunction(fd);
        break;
    }
    case DeclKind::Class: {
        auto &cd = static_cast<ClassDecl &>(decl);
        registerClass(cd);
        break;
    }
    case DeclKind::Interface: {
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
                error(typeNode.loc, "enum constant '" + resolved.enumValues[i] + "' is already defined");
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

    constants_[key] = type;

    // Store integer constant values for compile-time evaluation
    if (type.kind == PasTypeKind::Integer && value.kind == ExprKind::IntLiteral)
    {
        constantValues_[key] = static_cast<IntLiteralExpr &>(value).value;
    }
}

void SemanticAnalyzer::registerProcedure(ProcedureDecl &decl)
{
    // Method implementations (e.g., TClass.Method) use qualified names
    // and don't participate in free function overloading
    std::string key = decl.isMethod()
                          ? toLower(decl.className + "." + decl.name)
                          : toLower(decl.name);

    // v0.1: Check for user-defined overloading (not allowed for free procedures)
    if (!decl.isMethod())
    {
        auto existingIt = functions_.find(key);
        if (existingIt != functions_.end() && !existingIt->second.isForward && !decl.isForward)
        {
            error(decl.loc, "procedure '" + decl.name + "' is already defined; "
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

    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        sig.params.emplace_back(param.name, paramType);
        sig.isVarParam.push_back(param.isVar);
        sig.hasDefault.push_back(param.defaultValue != nullptr);
    }

    functions_[key] = sig;
}

void SemanticAnalyzer::registerFunction(FunctionDecl &decl)
{
    // Method implementations (e.g., TClass.Method) use qualified names
    // and don't participate in free function overloading
    std::string key = decl.isMethod()
                          ? toLower(decl.className + "." + decl.name)
                          : toLower(decl.name);

    // v0.1: Check for user-defined overloading (not allowed for free functions)
    if (!decl.isMethod())
    {
        auto existingIt = functions_.find(key);
        if (existingIt != functions_.end() && !existingIt->second.isForward && !decl.isForward)
        {
            error(decl.loc, "function '" + decl.name + "' is already defined; "
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

    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        sig.params.emplace_back(param.name, paramType);
        sig.isVarParam.push_back(param.isVar);
        sig.hasDefault.push_back(param.defaultValue != nullptr);
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

    // Process members
    for (const auto &member : decl.members)
    {
        switch (member.memberKind)
        {
        case ClassMember::Kind::Field: {
            FieldInfo field;
            field.name = member.fieldName;
            field.type = member.fieldType ? resolveType(*member.fieldType) : PasType::unknown();
            field.isWeak = member.isWeak;
            field.visibility = member.visibility;
            field.loc = member.loc;
            info.fields[toLower(member.fieldName)] = field;
            break;
        }
        case ClassMember::Kind::Method: {
            if (member.methodDecl)
            {
                if (member.methodDecl->kind == DeclKind::Function)
                {
                    auto &fd = static_cast<FunctionDecl &>(*member.methodDecl);
                    MethodInfo method;
                    method.name = fd.name;
                    method.returnType = fd.returnType ? resolveType(*fd.returnType) : PasType::unknown();
                    method.isVirtual = fd.isVirtual;
                    method.isOverride = fd.isOverride;
                    method.isAbstract = fd.isAbstract;
                    method.visibility = member.visibility;
                    method.loc = fd.loc;
                    for (const auto &param : fd.params)
                    {
                        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
                        method.params.emplace_back(param.name, paramType);
                        method.isVarParam.push_back(param.isVar);
                    }
                    info.methods[toLower(fd.name)] = method;
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
                    for (const auto &param : pd.params)
                    {
                        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
                        method.params.emplace_back(param.name, paramType);
                        method.isVarParam.push_back(param.isVar);
                    }
                    info.methods[toLower(pd.name)] = method;
                }
            }
            break;
        }
        case ClassMember::Kind::Constructor: {
            info.hasConstructor = true;
            // Also add constructor to methods for lookup
            if (member.methodDecl && member.methodDecl->kind == DeclKind::Constructor)
            {
                auto &cd = static_cast<ConstructorDecl &>(*member.methodDecl);
                MethodInfo method;
                method.name = cd.name;
                method.returnType = PasType::voidType();  // Constructors don't return a value
                method.visibility = member.visibility;
                method.loc = cd.loc;
                for (const auto &param : cd.params)
                {
                    PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
                    method.params.emplace_back(param.name, paramType);
                    method.isVarParam.push_back(param.isVar);
                    method.hasDefault.push_back(param.defaultValue != nullptr);
                }
                info.methods[toLower(cd.name)] = method;
            }
            break;
        }
        case ClassMember::Kind::Destructor: {
            info.hasDestructor = true;
            // Also add destructor to methods for lookup
            if (member.methodDecl && member.methodDecl->kind == DeclKind::Destructor)
            {
                auto &dd = static_cast<DestructorDecl &>(*member.methodDecl);
                MethodInfo method;
                method.name = dd.name;
                method.returnType = PasType::voidType();
                method.visibility = member.visibility;
                method.loc = dd.loc;
                info.methods[toLower(dd.name)] = method;
            }
            break;
        }
        default:
            break;
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
        info.methods[toLower(sig.name)] = method;
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
                // Note: Return type on constructor is handled by parser (constructors don't have return type)
                // If we want to check, the AST would need to capture any erroneous return type
                (void)ctor;
            }
        }
    }
}

//===----------------------------------------------------------------------===//
// Class/Interface Semantic Checks
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::checkClassSemantics()
{
    for (const auto &[key, classInfo] : classes_)
    {
        checkClassInfo(classInfo);
    }
}

void SemanticAnalyzer::checkClassInfo(const ClassInfo &classInfo)
{
    // Collect the effective base class and interfaces
    // The parser puts the first heritage item in baseClass, rest in interfaces
    // But if baseClass is actually an interface, treat it as such
    std::string effectiveBaseClass;
    std::vector<std::string> effectiveInterfaces = classInfo.interfaces;

    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (classes_.find(baseKey) != classes_.end())
        {
            // It's a class - use as base class
            effectiveBaseClass = classInfo.baseClass;
        }
        else if (interfaces_.find(baseKey) != interfaces_.end())
        {
            // It's an interface - add to interfaces list
            effectiveInterfaces.insert(effectiveInterfaces.begin(), classInfo.baseClass);
        }
        else
        {
            error(classInfo.loc, "unknown type '" + classInfo.baseClass + "' in heritage clause");
        }
    }

    // Check for multiple base classes (heritage list with >1 class)
    // We need to verify interfaces list items are actually interfaces, not classes
    for (const auto &ifaceName : effectiveInterfaces)
    {
        std::string key = toLower(ifaceName);
        // Check if it's a class (error) or interface (ok)
        if (classes_.find(key) != classes_.end())
        {
            error(classInfo.loc, "at most one base class permitted; '" + ifaceName +
                                     "' is a class, not an interface");
        }
        else if (interfaces_.find(key) == interfaces_.end())
        {
            error(classInfo.loc, "unknown interface '" + ifaceName + "'");
        }
    }

    // Check method overrides using effective base class
    checkOverridesWithBase(classInfo, effectiveBaseClass);

    // Check interface implementation using effective interfaces
    checkInterfaceImplementationWith(classInfo, effectiveInterfaces);

    // Check weak fields
    checkWeakFields(classInfo);
}

void SemanticAnalyzer::checkOverrides(const ClassInfo &classInfo)
{
    checkOverridesWithBase(classInfo, classInfo.baseClass);
}

void SemanticAnalyzer::checkOverridesWithBase(const ClassInfo &classInfo,
                                               const std::string &effectiveBaseClass)
{
    for (const auto &[methodKey, method] : classInfo.methods)
    {
        if (method.isOverride)
        {
            // Must find a virtual method in base class hierarchy
            auto baseMethod = findVirtualInBase(effectiveBaseClass, method.name);
            if (!baseMethod)
            {
                error(method.loc, "method '" + method.name +
                                      "' marked override but no virtual method found in base class");
            }
            else if (!signaturesMatch(method, *baseMethod))
            {
                error(method.loc, "override method '" + method.name +
                                      "' signature does not match base virtual method");
            }
        }
    }
}

std::optional<MethodInfo> SemanticAnalyzer::findVirtualInBase(const std::string &className,
                                                               const std::string &methodName) const
{
    if (className.empty())
        return std::nullopt;

    std::string classKey = toLower(className);
    auto it = classes_.find(classKey);
    if (it == classes_.end())
        return std::nullopt;

    const ClassInfo &baseClass = it->second;

    // Look for method in this class
    std::string methodKey = toLower(methodName);
    auto methodIt = baseClass.methods.find(methodKey);
    if (methodIt != baseClass.methods.end())
    {
        if (methodIt->second.isVirtual || methodIt->second.isAbstract)
        {
            return methodIt->second;
        }
    }

    // Recurse to parent
    return findVirtualInBase(baseClass.baseClass, methodName);
}

bool SemanticAnalyzer::signaturesMatch(const MethodInfo &m1, const MethodInfo &m2) const
{
    // Return types must match
    if (m1.returnType.kind != m2.returnType.kind)
        return false;

    // Parameter count must match
    if (m1.params.size() != m2.params.size())
        return false;

    // Each parameter type and var/out must match
    for (size_t i = 0; i < m1.params.size(); ++i)
    {
        if (m1.params[i].second.kind != m2.params[i].second.kind)
            return false;
        if (m1.isVarParam[i] != m2.isVarParam[i])
            return false;
    }

    return true;
}

void SemanticAnalyzer::checkInterfaceImplementation(const ClassInfo &classInfo)
{
    checkInterfaceImplementationWith(classInfo, classInfo.interfaces);
}

void SemanticAnalyzer::checkInterfaceImplementationWith(
    const ClassInfo &classInfo, const std::vector<std::string> &effectiveInterfaces)
{
    // Collect all methods required by interfaces
    std::map<std::string, MethodInfo> requiredMethods;
    for (const auto &ifaceName : effectiveInterfaces)
    {
        collectInterfaceMethods(ifaceName, requiredMethods);
    }

    // Check each required method is implemented
    for (const auto &[methodKey, ifaceMethod] : requiredMethods)
    {
        // Look in this class
        auto classMethodIt = classInfo.methods.find(methodKey);
        if (classMethodIt != classInfo.methods.end())
        {
            // Check signature
            if (!signaturesMatch(classMethodIt->second, ifaceMethod))
            {
                error(classInfo.loc, "method '" + ifaceMethod.name +
                                         "' signature does not match interface");
            }
            continue;
        }

        // Look in base class hierarchy
        bool found = false;
        std::string baseClass = classInfo.baseClass;
        while (!baseClass.empty() && !found)
        {
            std::string baseKey = toLower(baseClass);
            auto baseIt = classes_.find(baseKey);
            if (baseIt == classes_.end())
                break;

            auto baseMethodIt = baseIt->second.methods.find(methodKey);
            if (baseMethodIt != baseIt->second.methods.end())
            {
                if (signaturesMatch(baseMethodIt->second, ifaceMethod))
                {
                    found = true;
                }
                break;
            }
            baseClass = baseIt->second.baseClass;
        }

        if (!found)
        {
            error(classInfo.loc, "class '" + classInfo.name + "' does not implement interface method '" +
                                     ifaceMethod.name + "'");
        }
    }
}

void SemanticAnalyzer::collectInterfaceMethods(const std::string &ifaceName,
                                                std::map<std::string, MethodInfo> &methods) const
{
    std::string key = toLower(ifaceName);
    auto it = interfaces_.find(key);
    if (it == interfaces_.end())
        return;

    const InterfaceInfo &iface = it->second;

    // Add this interface's methods
    for (const auto &[methodKey, method] : iface.methods)
    {
        methods[methodKey] = method;
    }

    // Recurse to base interfaces
    for (const auto &baseIface : iface.baseInterfaces)
    {
        collectInterfaceMethods(baseIface, methods);
    }
}

void SemanticAnalyzer::checkWeakFields(const ClassInfo &classInfo)
{
    for (const auto &[fieldKey, field] : classInfo.fields)
    {
        if (field.isWeak)
        {
            // Check that the field type is a class or interface (or optional of such)
            PasType fieldType = field.type;
            if (fieldType.isOptional() && fieldType.innerType)
            {
                fieldType = *fieldType.innerType;
            }

            if (fieldType.kind != PasTypeKind::Class && fieldType.kind != PasTypeKind::Interface)
            {
                error(field.loc, "weak may only be applied to class/interface fields, not " +
                                     field.type.toString());
            }
        }
    }
}

bool SemanticAnalyzer::classImplementsInterface(const std::string &className,
                                                 const std::string &interfaceName) const
{
    if (className.empty() || interfaceName.empty())
        return false;

    std::string classKey = toLower(className);
    std::string ifaceKey = toLower(interfaceName);

    auto classIt = classes_.find(classKey);
    if (classIt == classes_.end())
        return false;

    const ClassInfo &classInfo = classIt->second;

    // Check directly implemented interfaces
    for (const auto &implIface : classInfo.interfaces)
    {
        if (toLower(implIface) == ifaceKey)
            return true;

        // Check if implemented interface extends target interface
        if (interfaceExtendsInterface(implIface, interfaceName))
            return true;
    }

    // Also check if "baseClass" is actually an interface (parser puts first parent there)
    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (interfaces_.find(baseKey) != interfaces_.end())
        {
            if (baseKey == ifaceKey || interfaceExtendsInterface(classInfo.baseClass, interfaceName))
                return true;
        }
    }

    // Recurse to base class
    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (classes_.find(baseKey) != classes_.end())
        {
            return classImplementsInterface(classInfo.baseClass, interfaceName);
        }
    }

    return false;
}

bool SemanticAnalyzer::classInheritsFrom(const std::string &derivedName,
                                          const std::string &baseName) const
{
    if (derivedName.empty() || baseName.empty())
        return false;

    // Same class (case-insensitive)
    if (toLower(derivedName) == toLower(baseName))
        return true;

    std::string derivedKey = toLower(derivedName);
    auto it = classes_.find(derivedKey);
    if (it == classes_.end())
        return false;

    const ClassInfo &classInfo = it->second;

    // Recurse to base class
    if (!classInfo.baseClass.empty())
    {
        return classInheritsFrom(classInfo.baseClass, baseName);
    }

    return false;
}

bool SemanticAnalyzer::interfaceExtendsInterface(const std::string &derivedName,
                                                  const std::string &baseName) const
{
    if (derivedName.empty() || baseName.empty())
        return false;

    // Same interface (case-insensitive)
    if (toLower(derivedName) == toLower(baseName))
        return true;

    std::string derivedKey = toLower(derivedName);
    auto it = interfaces_.find(derivedKey);
    if (it == interfaces_.end())
        return false;

    const InterfaceInfo &ifaceInfo = it->second;

    // Check base interfaces
    for (const auto &baseIface : ifaceInfo.baseInterfaces)
    {
        if (interfaceExtendsInterface(baseIface, baseName))
            return true;
    }

    return false;
}

const ClassInfo *SemanticAnalyzer::lookupClass(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = classes_.find(key);
    if (it != classes_.end())
        return &it->second;
    return nullptr;
}

const InterfaceInfo *SemanticAnalyzer::lookupInterface(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = interfaces_.find(key);
    if (it != interfaces_.end())
        return &it->second;
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Body Analysis (Pass 2)
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::analyzeBodies(Program &prog)
{
    // Analyze procedure/function/constructor/destructor bodies
    for (auto &decl : prog.decls)
    {
        if (!decl)
            continue;
        if (decl->kind == DeclKind::Procedure)
        {
            analyzeProcedureBody(static_cast<ProcedureDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Function)
        {
            analyzeFunctionBody(static_cast<FunctionDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            analyzeConstructorBody(static_cast<ConstructorDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            analyzeDestructorBody(static_cast<DestructorDecl &>(*decl));
        }
    }

    // Analyze main program body
    if (prog.body)
    {
        analyzeBlock(*prog.body);
    }
}

void SemanticAnalyzer::analyzeBodies(Unit &unit)
{
    // Analyze implementation bodies
    for (auto &decl : unit.implDecls)
    {
        if (!decl)
            continue;
        if (decl->kind == DeclKind::Procedure)
        {
            analyzeProcedureBody(static_cast<ProcedureDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Function)
        {
            analyzeFunctionBody(static_cast<FunctionDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            analyzeConstructorBody(static_cast<ConstructorDecl &>(*decl));
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            analyzeDestructorBody(static_cast<DestructorDecl &>(*decl));
        }
    }

    // Analyze init section
    if (unit.initSection)
    {
        analyzeBlock(*unit.initSection);
    }

    // Analyze final section
    if (unit.finalSection)
    {
        analyzeBlock(*unit.finalSection);
    }
}

void SemanticAnalyzer::analyzeProcedureBody(ProcedureDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class if this is a method
    std::string savedClassName = currentClassName_;
    if (decl.isMethod())
    {
        currentClassName_ = decl.className;
    }

    // Push scope for procedure
    pushScope();
    ++routineDepth_;

    // Register Self for methods
    if (decl.isMethod())
    {
        addVariable("self", PasType::classType(decl.className));
    }

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // v0.1: Reject nested procedures/functions
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
        {
            if (localDecl->kind == DeclKind::Procedure || localDecl->kind == DeclKind::Function)
            {
                error(localDecl->loc, "nested procedures/functions are not supported in Viper Pascal v0.1; "
                                      "move declarations to the enclosing scope");
            }
            else
            {
                collectDecl(*localDecl);
            }
        }
    }

    // Analyze body
    analyzeBlock(*decl.body);

    --routineDepth_;
    popScope();
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeFunctionBody(FunctionDecl &decl)
{
    if (!decl.body)
        return;

    // Set current function for return type checking
    std::string key = toLower(decl.name);
    auto it = functions_.find(key);
    if (it != functions_.end())
    {
        currentFunction_ = &it->second;
    }

    // Set current class if this is a method
    std::string savedClassName = currentClassName_;
    if (decl.isMethod())
    {
        currentClassName_ = decl.className;
    }

    // Push scope for function
    pushScope();
    ++routineDepth_;

    // Register Self for methods
    if (decl.isMethod())
    {
        addVariable("self", PasType::classType(decl.className));
    }

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // Register Result variable with function's return type
    // Note: Per spec, assigning to function name is NOT supported - only Result
    PasType retType = decl.returnType ? resolveType(*decl.returnType) : PasType::unknown();
    addVariable("result", retType);

    // v0.1: Reject nested procedures/functions
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
        {
            if (localDecl->kind == DeclKind::Procedure || localDecl->kind == DeclKind::Function)
            {
                error(localDecl->loc, "nested procedures/functions are not supported in Viper Pascal v0.1; "
                                      "move declarations to the enclosing scope");
            }
            else
            {
                collectDecl(*localDecl);
            }
        }
    }

    // Analyze body
    analyzeBlock(*decl.body);

    --routineDepth_;
    popScope();
    currentFunction_ = nullptr;
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeConstructorBody(ConstructorDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class
    std::string savedClassName = currentClassName_;
    currentClassName_ = decl.className;

    // Push scope for constructor
    pushScope();

    // Register Self with the class type
    addVariable("self", PasType::classType(decl.className));

    // Register parameters
    for (const auto &param : decl.params)
    {
        PasType paramType = param.type ? resolveType(*param.type) : PasType::unknown();
        addVariable(toLower(param.name), paramType);
    }

    // Register local declarations
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
            collectDecl(*localDecl);
    }

    // Analyze body
    analyzeBlock(*decl.body);

    popScope();
    currentClassName_ = savedClassName;
}

void SemanticAnalyzer::analyzeDestructorBody(DestructorDecl &decl)
{
    if (!decl.body)
        return;

    // Set current class
    std::string savedClassName = currentClassName_;
    currentClassName_ = decl.className;

    // Push scope for destructor
    pushScope();

    // Register Self with the class type
    addVariable("self", PasType::classType(decl.className));

    // Register local declarations (destructors have no parameters)
    for (auto &localDecl : decl.localDecls)
    {
        if (localDecl)
            collectDecl(*localDecl);
    }

    // Analyze body
    analyzeBlock(*decl.body);

    popScope();
    currentClassName_ = savedClassName;
}

//===----------------------------------------------------------------------===//
// Statement Analysis
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::analyzeStmt(Stmt &stmt)
{
    switch (stmt.kind)
    {
    case StmtKind::Block:
        analyzeBlock(static_cast<BlockStmt &>(stmt));
        break;
    case StmtKind::Assign:
        analyzeAssign(static_cast<AssignStmt &>(stmt));
        break;
    case StmtKind::Call:
        analyzeCall(static_cast<CallStmt &>(stmt));
        break;
    case StmtKind::If:
        analyzeIf(static_cast<IfStmt &>(stmt));
        break;
    case StmtKind::While:
        analyzeWhile(static_cast<WhileStmt &>(stmt));
        break;
    case StmtKind::Repeat:
        analyzeRepeat(static_cast<RepeatStmt &>(stmt));
        break;
    case StmtKind::For:
        analyzeFor(static_cast<ForStmt &>(stmt));
        break;
    case StmtKind::ForIn:
        analyzeForIn(static_cast<ForInStmt &>(stmt));
        break;
    case StmtKind::Case:
        analyzeCase(static_cast<CaseStmt &>(stmt));
        break;
    case StmtKind::Break:
        if (loopDepth_ == 0)
        {
            error(stmt, "break statement outside of loop");
        }
        break;
    case StmtKind::Continue:
        if (loopDepth_ == 0)
        {
            error(stmt, "continue statement outside of loop");
        }
        break;
    case StmtKind::Exit:
        analyzeExit(static_cast<ExitStmt &>(stmt));
        break;
    case StmtKind::Raise:
        analyzeRaise(static_cast<RaiseStmt &>(stmt));
        break;
    case StmtKind::TryExcept:
        analyzeTryExcept(static_cast<TryExceptStmt &>(stmt));
        break;
    case StmtKind::TryFinally:
        analyzeTryFinally(static_cast<TryFinallyStmt &>(stmt));
        break;
    case StmtKind::With:
        analyzeWith(static_cast<WithStmt &>(stmt));
        break;
    case StmtKind::Inherited:
        analyzeInherited(static_cast<InheritedStmt &>(stmt));
        break;
    case StmtKind::Empty:
        // Nothing to analyze
        break;
    }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt &block)
{
    for (auto &stmt : block.stmts)
    {
        if (stmt)
            analyzeStmt(*stmt);
    }
}

void SemanticAnalyzer::analyzeAssign(AssignStmt &stmt)
{
    if (!stmt.target || !stmt.value)
        return;

    // Check for assignment to read-only loop variable
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        std::string key = toLower(nameExpr.name);
        if (readOnlyLoopVars_.count(key))
        {
            error(stmt, "cannot assign to loop variable '" + nameExpr.name + "' inside loop body");
            return;
        }

        // Check for assignment to function name (not allowed - use Result instead)
        if (functions_.count(key))
        {
            error(stmt, "cannot assign to function name '" + nameExpr.name +
                            "'; use 'Result' to return a value");
            return;
        }
    }

    // For assignment targets, use the declared type (not narrowed type)
    // Narrowing only affects reads, not assignment targets
    PasType targetType;
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        // Use lookupVariable to get declared type, not lookupEffectiveType
        auto declaredType = lookupVariable(nameExpr.name);
        if (declaredType)
        {
            targetType = *declaredType;
        }
        else
        {
            targetType = typeOf(*stmt.target);
        }
    }
    else
    {
        targetType = typeOf(*stmt.target);
    }

    PasType valueType = typeOf(*stmt.value);

    // Special check: non-optional class/interface cannot be assigned nil
    if (valueType.kind == PasTypeKind::Nil &&
        (targetType.kind == PasTypeKind::Class || targetType.kind == PasTypeKind::Interface) &&
        !targetType.isOptional())
    {
        error(stmt, "cannot assign nil to non-optional " + targetType.toString());
        return;
    }

    // Check assignability
    if (!isAssignableFrom(targetType, valueType))
    {
        error(stmt, "cannot assign " + valueType.toString() + " to " + targetType.toString());
    }

    // Invalidate narrowing and mark as definitely assigned if assigning to a variable
    if (stmt.target->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        invalidateNarrowing(nameExpr.name);

        // Mark as definitely assigned (removes from uninitializedNonNullableVars_)
        markDefinitelyAssigned(nameExpr.name);
    }
}

void SemanticAnalyzer::analyzeCall(CallStmt &stmt)
{
    if (!stmt.call)
        return;

    // The call expression must be a CallExpr
    if (stmt.call->kind != ExprKind::Call)
    {
        error(stmt, "statement must be a procedure call, not a bare expression");
        return;
    }

    // Type-check the call expression
    typeOf(*stmt.call);
}

void SemanticAnalyzer::analyzeIf(IfStmt &stmt)
{
    std::string narrowedVar;
    bool isNotNil = false;
    bool hasNilCheck = false;
    PasType unwrappedType;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition,
                  "condition must be Boolean, got " + condType.toString());
        }

        // Check for nil check pattern for flow narrowing
        if (isNilCheck(*stmt.condition, narrowedVar, isNotNil))
        {
            // Look up the variable's type
            if (auto varType = lookupVariable(narrowedVar))
            {
                if (varType->isOptional())
                {
                    hasNilCheck = true;
                    unwrappedType = varType->unwrap();
                }
            }
        }
    }

    // Save state before branches for definite assignment tracking
    std::set<std::string> uninitBeforeIf = uninitializedNonNullableVars_;

    // Analyze then-branch with narrowing if applicable
    if (stmt.thenBranch)
    {
        if (hasNilCheck && isNotNil)
        {
            // "x <> nil" - narrow x to T in then-branch
            std::map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.thenBranch);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.thenBranch);
        }
    }

    // Save what was initialized in the then branch
    std::set<std::string> uninitAfterThen = uninitializedNonNullableVars_;

    // Restore state before analyzing else branch
    uninitializedNonNullableVars_ = uninitBeforeIf;

    // Analyze else-branch with narrowing if applicable
    if (stmt.elseBranch)
    {
        if (hasNilCheck && !isNotNil)
        {
            // "x = nil" - narrow x to T in else-branch
            std::map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.elseBranch);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.elseBranch);
        }
    }

    std::set<std::string> uninitAfterElse = uninitializedNonNullableVars_;

    // Compute union: a variable is NOT definitely assigned after the if
    // if it was NOT assigned in at least one branch.
    // In other words: a variable is definitely assigned only if assigned in BOTH branches.
    if (stmt.thenBranch && stmt.elseBranch)
    {
        // A variable remains uninitialized if it's uninitialized after EITHER branch
        // (union of uninitAfterThen and uninitAfterElse)
        uninitializedNonNullableVars_ = uninitAfterThen;
        for (const auto &var : uninitAfterElse)
        {
            uninitializedNonNullableVars_.insert(var);
        }
    }
    else if (stmt.thenBranch)
    {
        // No else branch: can't assume then-branch executed
        // Conservatively keep the original uninitialized set
        uninitializedNonNullableVars_ = uninitBeforeIf;
    }
    else
    {
        // No branches at all (shouldn't happen but handle it)
        uninitializedNonNullableVars_ = uninitBeforeIf;
    }
}

void SemanticAnalyzer::analyzeWhile(WhileStmt &stmt)
{
    std::string narrowedVar;
    bool isNotNil = false;
    bool hasNilCheck = false;
    PasType unwrappedType;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition,
                  "condition must be Boolean, got " + condType.toString());
        }

        // Check for nil check pattern for flow narrowing
        if (isNilCheck(*stmt.condition, narrowedVar, isNotNil))
        {
            if (auto varType = lookupVariable(narrowedVar))
            {
                if (varType->isOptional())
                {
                    hasNilCheck = true;
                    unwrappedType = varType->unwrap();
                }
            }
        }
    }

    ++loopDepth_;
    if (stmt.body)
    {
        if (hasNilCheck && isNotNil)
        {
            // "while x <> nil" - narrow x to T in body
            std::map<std::string, PasType> narrowed;
            narrowed[narrowedVar] = unwrappedType;
            pushNarrowing(narrowed);
            analyzeStmt(*stmt.body);
            popNarrowing();
        }
        else
        {
            analyzeStmt(*stmt.body);
        }
    }
    --loopDepth_;
}

void SemanticAnalyzer::analyzeRepeat(RepeatStmt &stmt)
{
    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    if (stmt.condition)
    {
        PasType condType = typeOf(*stmt.condition);
        if (condType.kind != PasTypeKind::Boolean && !condType.isError())
        {
            error(*stmt.condition,
                  "condition must be Boolean, got " + condType.toString());
        }
    }
}

void SemanticAnalyzer::analyzeFor(ForStmt &stmt)
{
    // Look up or declare the loop variable
    std::string varKey = toLower(stmt.loopVar);
    auto varType = lookupVariable(varKey);

    if (!varType)
    {
        // Implicitly declare as Integer
        addVariable(varKey, PasType::integer());
        varType = PasType::integer();
    }

    // Loop variable must be ordinal (Integer or enum), not Real
    if (!varType->isOrdinal())
    {
        error(stmt, "for loop variable must be Integer or enum type (not Real)");
    }

    // Check start and bound expressions
    if (stmt.start)
    {
        PasType startType = typeOf(*stmt.start);
        if (!isAssignableFrom(*varType, startType) && !startType.isError())
        {
            error(*stmt.start, "start value type mismatch");
        }
    }

    if (stmt.bound)
    {
        PasType boundType = typeOf(*stmt.bound);
        if (!isAssignableFrom(*varType, boundType) && !boundType.isError())
        {
            error(*stmt.bound, "bound value type mismatch");
        }
    }

    // Mark the loop variable as read-only during body analysis
    readOnlyLoopVars_.insert(varKey);
    // Remove from undefined set (it's valid inside the loop)
    undefinedVars_.erase(varKey);

    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    // After the loop, the loop variable becomes undefined
    readOnlyLoopVars_.erase(varKey);
    undefinedVars_.insert(varKey);
}

void SemanticAnalyzer::analyzeForIn(ForInStmt &stmt)
{
    // Type-check collection
    PasType collType = stmt.collection ? typeOf(*stmt.collection) : PasType::unknown();

    // Infer element type based on collection type
    PasType elementType = PasType::unknown();
    bool validIterable = false;

    if (!collType.isError())
    {
        if (collType.kind == PasTypeKind::Array && collType.elementType)
        {
            // Array iteration yields element type
            elementType = *collType.elementType;
            validIterable = true;
        }
        else if (collType.kind == PasTypeKind::String)
        {
            // String iteration yields 1-character strings
            elementType = PasType::string();
            validIterable = true;
        }
        else
        {
            error(stmt, "for-in requires an array or string, got " + collType.toString());
        }
    }

    // Create a new scope for the loop variable
    pushScope();

    // Declare the loop variable with inferred element type
    std::string varKey = toLower(stmt.loopVar);
    if (validIterable)
    {
        addVariable(varKey, elementType);
    }
    else
    {
        addVariable(varKey, PasType::unknown());
    }

    // Mark the loop variable as read-only during body analysis
    readOnlyLoopVars_.insert(varKey);

    ++loopDepth_;
    if (stmt.body)
        analyzeStmt(*stmt.body);
    --loopDepth_;

    // After the loop, the loop variable becomes undefined
    readOnlyLoopVars_.erase(varKey);

    // Pop the scope (the variable is no longer accessible)
    popScope();
}

void SemanticAnalyzer::analyzeCase(CaseStmt &stmt)
{
    PasType exprType = PasType::unknown();
    if (stmt.expr)
    {
        exprType = typeOf(*stmt.expr);
        // Case expression must be Integer or Enum (not String in v0.1)
        if (!exprType.isError() && exprType.kind != PasTypeKind::Integer &&
            exprType.kind != PasTypeKind::Enum)
        {
            error(*stmt.expr, "case expression must be Integer or enum type");
        }
    }

    // Track seen labels for duplicate detection
    std::set<int64_t> seenLabels;

    for (auto &arm : stmt.arms)
    {
        for (auto &label : arm.labels)
        {
            if (!label)
                continue;

            // Type-check the label
            PasType labelType = typeOf(*label);

            // Check label type matches case expression type
            if (!labelType.isError() && !exprType.isError())
            {
                if (exprType.kind == PasTypeKind::Integer && labelType.kind != PasTypeKind::Integer)
                {
                    error(*label, "case label must be Integer");
                }
                else if (exprType.kind == PasTypeKind::Enum)
                {
                    if (labelType.kind != PasTypeKind::Enum || labelType.name != exprType.name)
                    {
                        error(*label, "case label must be of type " + exprType.name);
                    }
                }
            }

            // Extract compile-time constant value for duplicate detection
            int64_t labelValue = 0;
            bool isConstant = false;
            if (label->kind == ExprKind::IntLiteral)
            {
                labelValue = static_cast<IntLiteralExpr &>(*label).value;
                isConstant = true;
            }
            else if (label->kind == ExprKind::Name)
            {
                // Check if it's an enum constant
                auto &nameExpr = static_cast<NameExpr &>(*label);
                if (auto constType = lookupConstant(toLower(nameExpr.name)))
                {
                    if (constType->kind == PasTypeKind::Enum && constType->enumOrdinal >= 0)
                    {
                        labelValue = constType->enumOrdinal;
                        isConstant = true;
                    }
                }
            }

            // Check for duplicates
            if (isConstant)
            {
                if (seenLabels.count(labelValue) > 0)
                {
                    error(*label, "duplicate case label");
                }
                else
                {
                    seenLabels.insert(labelValue);
                }
            }
        }
        if (arm.body)
            analyzeStmt(*arm.body);
    }

    if (stmt.elseBody)
        analyzeStmt(*stmt.elseBody);
}

void SemanticAnalyzer::analyzeRaise(RaiseStmt &stmt)
{
    if (stmt.exception)
    {
        // raise Expr; - type-check the exception expression
        PasType excType = typeOf(*stmt.exception);

        // Check that the expression is an exception type (class derived from Exception)
        if (!excType.isError())
        {
            if (excType.kind != PasTypeKind::Class)
            {
                error(stmt, "raise expression must be an exception object (class type)");
            }
            else
            {
                // Verify the class derives from Exception
                bool derivesFromException = false;
                std::string checkClass = toLower(excType.name);
                while (!checkClass.empty())
                {
                    if (checkClass == "exception")
                    {
                        derivesFromException = true;
                        break;
                    }
                    auto classIt = classes_.find(checkClass);
                    if (classIt == classes_.end())
                        break;
                    checkClass = toLower(classIt->second.baseClass);
                }
                if (!derivesFromException)
                {
                    error(stmt, "raise expression must be of type Exception or a subclass, not '" +
                                    excType.name + "'");
                }
            }
        }
    }
    else
    {
        // raise; (re-raise) - only valid inside except handler
        if (exceptHandlerDepth_ == 0)
        {
            error(stmt, "'raise' without expression is only valid inside an except handler");
        }
    }
}

void SemanticAnalyzer::analyzeExit(ExitStmt &stmt)
{
    // Exit must be inside a procedure/function
    if (routineDepth_ == 0)
    {
        error(stmt, "'Exit' statement is only valid inside a procedure or function");
        return;
    }

    if (stmt.value)
    {
        // Exit(value) - must be inside a function, and value type must match return type
        if (!currentFunction_)
        {
            error(stmt, "'Exit' with a value is only valid inside a function");
            return;
        }

        if (currentFunction_->returnType.kind == PasTypeKind::Void)
        {
            error(stmt, "'Exit' with a value is not valid in a procedure (use 'Exit;' instead)");
            return;
        }

        PasType valType = typeOf(*stmt.value);
        if (!valType.isError() && !isAssignableFrom(currentFunction_->returnType, valType))
        {
            error(stmt, "Exit value type '" + valType.toString() +
                            "' is not compatible with function return type '" +
                            currentFunction_->returnType.toString() + "'");
        }
    }
    // Exit; without value is valid in both procedures and functions
    // In functions, it returns the current value of 'Result' (or undefined if not set)
}

void SemanticAnalyzer::analyzeTryExcept(TryExceptStmt &stmt)
{
    // Reject except...else syntax in v0.1
    if (stmt.elseBody)
    {
        error(stmt, "'except...else' is not supported; use 'on E: Exception do' as a catch-all");
        // Continue analysis for better error recovery
    }

    if (stmt.tryBody)
        analyzeBlock(*stmt.tryBody);

    for (auto &handler : stmt.handlers)
    {
        // Validate handler type derives from Exception
        std::string typeLower = toLower(handler.typeName);
        auto typeIt = types_.find(typeLower);
        if (typeIt == types_.end())
        {
            error(handler.loc, "unknown exception type '" + handler.typeName + "'");
        }
        else if (typeIt->second.kind != PasTypeKind::Class)
        {
            error(handler.loc,
                  "exception handler type must be a class, not '" + handler.typeName + "'");
        }
        else
        {
            // Check if type is Exception or derives from it
            // For now, we accept any class type; full inheritance checking would
            // walk the class hierarchy to verify it derives from Exception
            bool derivesFromException = false;
            std::string checkClass = typeLower;
            while (!checkClass.empty())
            {
                if (checkClass == "exception")
                {
                    derivesFromException = true;
                    break;
                }
                auto classIt = classes_.find(checkClass);
                if (classIt == classes_.end())
                    break;
                checkClass = toLower(classIt->second.baseClass);
            }
            if (!derivesFromException)
            {
                error(handler.loc, "exception handler type '" + handler.typeName +
                                       "' must derive from Exception");
            }
        }

        pushScope();
        if (!handler.varName.empty())
        {
            // Register exception variable
            PasType excType;
            excType.kind = PasTypeKind::Class;
            excType.name = handler.typeName;
            addVariable(toLower(handler.varName), excType);
        }

        // Track that we're inside an except handler for raise; validation
        exceptHandlerDepth_++;
        if (handler.body)
            analyzeStmt(*handler.body);
        exceptHandlerDepth_--;

        popScope();
    }

    if (stmt.elseBody)
        analyzeStmt(*stmt.elseBody);
}

void SemanticAnalyzer::analyzeTryFinally(TryFinallyStmt &stmt)
{
    if (stmt.tryBody)
        analyzeBlock(*stmt.tryBody);
    if (stmt.finallyBody)
        analyzeBlock(*stmt.finallyBody);
}

void SemanticAnalyzer::analyzeWith(WithStmt &stmt)
{
    // v0.1: with statement is not supported
    error(stmt, "'with' statement is not supported in Viper Pascal v0.1");
}

void SemanticAnalyzer::analyzeInherited(InheritedStmt &stmt)
{
    // inherited must be used inside a method
    if (currentClassName_.empty())
    {
        error(stmt, "'inherited' can only be used inside a method");
        return;
    }

    // Look up the current class to find its base class
    std::string classKey = toLower(currentClassName_);
    auto classIt = classes_.find(classKey);
    if (classIt == classes_.end())
    {
        error(stmt, "internal error: current class '" + currentClassName_ + "' not found");
        return;
    }

    const ClassInfo &classInfo = classIt->second;
    if (classInfo.baseClass.empty())
    {
        error(stmt, "cannot use 'inherited' - class '" + currentClassName_ + "' has no base class");
        return;
    }

    // Type-check arguments
    for (auto &arg : stmt.args)
    {
        if (arg)
            typeOf(*arg);
    }

    // TODO: If methodName is specified, verify it exists in base class hierarchy
    // For now, we just type-check the arguments
}

//===----------------------------------------------------------------------===//
// Expression Type Checking
//===----------------------------------------------------------------------===//

PasType SemanticAnalyzer::typeOf(Expr &expr)
{
    switch (expr.kind)
    {
    case ExprKind::IntLiteral:
        return typeOfIntLiteral(static_cast<IntLiteralExpr &>(expr));
    case ExprKind::RealLiteral:
        return typeOfRealLiteral(static_cast<RealLiteralExpr &>(expr));
    case ExprKind::StringLiteral:
        return typeOfStringLiteral(static_cast<StringLiteralExpr &>(expr));
    case ExprKind::BoolLiteral:
        return typeOfBoolLiteral(static_cast<BoolLiteralExpr &>(expr));
    case ExprKind::NilLiteral:
        return typeOfNil(static_cast<NilLiteralExpr &>(expr));
    case ExprKind::Name:
        return typeOfName(static_cast<NameExpr &>(expr));
    case ExprKind::Unary:
        return typeOfUnary(static_cast<UnaryExpr &>(expr));
    case ExprKind::Binary:
        return typeOfBinary(static_cast<BinaryExpr &>(expr));
    case ExprKind::Call:
        return typeOfCall(static_cast<CallExpr &>(expr));
    case ExprKind::Index:
        return typeOfIndex(static_cast<IndexExpr &>(expr));
    case ExprKind::Field:
        return typeOfField(static_cast<FieldExpr &>(expr));
    case ExprKind::TypeCast:
        return typeOfTypeCast(static_cast<TypeCastExpr &>(expr));
    case ExprKind::SetConstructor:
        return typeOfSetConstructor(static_cast<SetConstructorExpr &>(expr));
    case ExprKind::AddressOf:
        return typeOfAddressOf(static_cast<AddressOfExpr &>(expr));
    case ExprKind::Dereference:
        return typeOfDereference(static_cast<DereferenceExpr &>(expr));
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfIntLiteral(IntLiteralExpr & /*expr*/)
{
    return PasType::integer();
}

PasType SemanticAnalyzer::typeOfRealLiteral(RealLiteralExpr & /*expr*/)
{
    return PasType::real();
}

PasType SemanticAnalyzer::typeOfStringLiteral(StringLiteralExpr & /*expr*/)
{
    return PasType::string();
}

PasType SemanticAnalyzer::typeOfBoolLiteral(BoolLiteralExpr & /*expr*/)
{
    return PasType::boolean();
}

PasType SemanticAnalyzer::typeOfNil(NilLiteralExpr & /*expr*/)
{
    return PasType::nil();
}

PasType SemanticAnalyzer::typeOfName(NameExpr &expr)
{
    std::string key = toLower(expr.name);

    // Check if the variable is undefined (e.g., for loop variable after loop ends)
    if (undefinedVars_.count(key))
    {
        error(expr, "loop variable '" + expr.name + "' is undefined after loop terminates");
        return PasType::unknown();
    }

    // Check definite assignment for non-nullable reference locals
    if (uninitializedNonNullableVars_.count(key))
    {
        error(expr, "variable '" + expr.name + "' may not have been initialized");
        return PasType::unknown();
    }

    // Check variables first, using effective type (respects narrowing)
    if (auto type = lookupEffectiveType(key))
    {
        return *type;
    }

    // Check constants
    if (auto type = lookupConstant(key))
    {
        return *type;
    }

    // Check if it's a type name (for type references)
    if (auto type = lookupType(key))
    {
        return *type;
    }

    // Check for zero-argument builtin functions (Pascal allows calling without parens)
    if (auto builtinOpt = lookupBuiltin(key))
    {
        const auto &desc = getBuiltinDescriptor(*builtinOpt);
        // Only allow if it can be called with 0 args and has non-void return type
        if (desc.minArgs == 0 && desc.result != ResultKind::Void)
        {
            return getBuiltinResultType(*builtinOpt);
        }
    }

    // Check for zero-argument user-defined functions (Pascal allows calling without parens)
    if (auto sig = lookupFunction(key))
    {
        // Only allow if it can be called with 0 args and has non-void return type
        if (sig->requiredParams == 0 && sig->returnType.kind != PasTypeKind::Void)
        {
            return sig->returnType;
        }
    }

    // Check if we're inside a class method and the name is a field of the current class
    if (!currentClassName_.empty())
    {
        auto *classInfo = lookupClass(toLower(currentClassName_));
        if (classInfo)
        {
            auto fieldIt = classInfo->fields.find(key);
            if (fieldIt != classInfo->fields.end())
            {
                return fieldIt->second.type;
            }
        }
    }

    error(expr, "undefined identifier '" + expr.name + "'");
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfUnary(UnaryExpr &expr)
{
    if (!expr.operand)
        return PasType::unknown();

    PasType operandType = typeOf(*expr.operand);
    PasType result = unaryResultType(expr.op, operandType);
    if (result.isError() && !operandType.isError())
    {
        switch (expr.op)
        {
        case UnaryExpr::Op::Not:
            error(expr, "operand must be Boolean for 'not'");
            break;
        case UnaryExpr::Op::Neg:
        case UnaryExpr::Op::Plus:
            error(expr, "operand must be numeric for unary +/-");
            break;
        }
    }
    return result;
}

PasType SemanticAnalyzer::typeOfBinary(BinaryExpr &expr)
{
    if (!expr.left || !expr.right)
        return PasType::unknown();

    PasType leftType = typeOf(*expr.left);
    PasType rightType = typeOf(*expr.right);

    PasType result = binaryResultType(expr.op, leftType, rightType);
    if (result.isError() && !leftType.isError() && !rightType.isError())
    {
        // Report the specific error based on operator
        switch (expr.op)
        {
        case BinaryExpr::Op::And:
        case BinaryExpr::Op::Or:
            error(expr, "operands must be Boolean for 'and'/'or'");
            break;
        case BinaryExpr::Op::IntDiv:
        case BinaryExpr::Op::Mod:
            error(expr, "operands must be Integer for 'div'/'mod'");
            break;
        case BinaryExpr::Op::Eq:
        case BinaryExpr::Op::Ne:
            // Check if this was a nil comparison with non-optional reference type
            if ((leftType.kind == PasTypeKind::Nil &&
                 (rightType.kind == PasTypeKind::Class || rightType.kind == PasTypeKind::Interface)) ||
                (rightType.kind == PasTypeKind::Nil &&
                 (leftType.kind == PasTypeKind::Class || leftType.kind == PasTypeKind::Interface)))
            {
                error(expr, "non-optional class cannot be compared to nil");
            }
            else
            {
                error(expr, "type mismatch in comparison");
            }
            break;
        case BinaryExpr::Op::Coalesce:
            error(expr, "type mismatch in nil coalescing expression");
            break;
        default:
            error(expr, "type mismatch in binary expression");
            break;
        }
    }
    return result;
}

PasType SemanticAnalyzer::typeOfCall(CallExpr &expr)
{
    if (!expr.callee)
        return PasType::unknown();

    // Get callee name and signature
    std::string calleeName;
    const FuncSignature *sig = nullptr;
    bool isMethodCall = false;
    std::string className;

    if (expr.callee->kind == ExprKind::Name)
    {
        calleeName = static_cast<NameExpr &>(*expr.callee).name;
    }
    else if (expr.callee->kind == ExprKind::Field)
    {
        // Method call or constructor call - get the method name and receiver type
        auto &fieldExpr = static_cast<FieldExpr &>(*expr.callee);
        calleeName = fieldExpr.field;
        isMethodCall = true;

        // Check if the base is a type reference (for constructor calls like TClassName.Create)
        bool isConstructorCall = false;
        if (fieldExpr.base && fieldExpr.base->kind == ExprKind::Name)
        {
            const auto &baseName = static_cast<const NameExpr &>(*fieldExpr.base);
            std::string baseKey = toLower(baseName.name);

            // Check if this is a type name (not a variable)
            if (!lookupVariable(baseKey) && !lookupConstant(baseKey))
            {
                if (auto typeOpt = lookupType(baseKey))
                {
                    if (typeOpt->kind == PasTypeKind::Class)
                    {
                        // This is a constructor call: ClassName.Create()
                        isConstructorCall = true;
                        className = typeOpt->name;

                        // Mark the expression for the lowerer
                        expr.isConstructorCall = true;
                        expr.constructorClassName = className;

                        // Look up the constructor in the class
                        auto *classInfo = lookupClass(baseKey);
                        if (classInfo)
                        {
                            std::string methodKey = toLower(calleeName);
                            auto methodIt = classInfo->methods.find(methodKey);
                            if (methodIt != classInfo->methods.end())
                            {
                                // Constructor found - return the class type (new instance)
                                // Type-check arguments
                                for (auto &arg : expr.args)
                                {
                                    if (arg)
                                        typeOf(*arg);
                                }
                                return PasType::classType(className);
                            }
                            else
                            {
                                error(expr, "class '" + className + "' has no constructor named '" + calleeName + "'");
                                return PasType::unknown();
                            }
                        }
                    }
                }
            }
        }

        // Regular method call on an instance
        if (!isConstructorCall)
        {
            // Type-check the receiver
            PasType receiverType = typeOf(*fieldExpr.base);

            // Look up the method in the class
            if (receiverType.kind == PasTypeKind::Class)
            {
                className = receiverType.name;
                // Methods are stored with qualified keys
                std::string qualifiedKey = toLower(className + "." + calleeName);
                sig = lookupFunction(qualifiedKey);

                // If not found directly, check the class info for inherited methods
                if (!sig)
                {
                    auto *classInfo = lookupClass(toLower(className));
                    if (classInfo)
                    {
                        std::string methodKey = toLower(calleeName);
                        auto methodIt = classInfo->methods.find(methodKey);
                        if (methodIt != classInfo->methods.end())
                        {
                            // Create a temporary signature from method info
                            // Note: This is a simplification - methods are properly stored
                            // in functions_ with qualified names
                            // For now, just allow the call if the method exists
                            // Type checking for args happens below with the signature
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Complex callee expression
        typeOf(*expr.callee);
        return PasType::unknown();
    }

    // For non-method calls, look up in global functions
    if (!isMethodCall)
    {
        std::string key = toLower(calleeName);
        sig = lookupFunction(key);

        if (!sig)
        {
            // Check if it's a variable/constant - give a better error message
            if (lookupVariable(key) || lookupConstant(key))
            {
                error(expr, "'" + calleeName + "' is not a procedure or function; "
                            "only calls are allowed as statements");
            }
            else
            {
                error(expr, "undefined procedure or function '" + calleeName + "'");
            }
            return PasType::unknown();
        }
    }
    else if (!sig)
    {
        // Method call - look up with qualified name
        std::string qualifiedKey = toLower(className + "." + calleeName);
        sig = lookupFunction(qualifiedKey);

        if (!sig)
        {
            // Check in class methods directly
            auto *classInfo = lookupClass(toLower(className));
            if (classInfo)
            {
                std::string methodKey = toLower(calleeName);
                auto methodIt = classInfo->methods.find(methodKey);
                if (methodIt != classInfo->methods.end())
                {
                    // For methods declared in class, use the method info
                    // Type-check args against the method's parameters
                    const MethodInfo &methodInfo = methodIt->second;

                    // Check argument count
                    size_t totalParams = methodInfo.params.size();
                    size_t requiredParams = methodInfo.requiredParams;
                    size_t actual = expr.args.size();

                    if (actual < requiredParams)
                    {
                        error(expr, "too few arguments: expected at least " +
                                        std::to_string(requiredParams) + ", got " + std::to_string(actual));
                    }
                    else if (actual > totalParams)
                    {
                        error(expr, "too many arguments: expected at most " +
                                        std::to_string(totalParams) + ", got " + std::to_string(actual));
                    }

                    // Type-check arguments
                    for (size_t i = 0; i < expr.args.size() && i < methodInfo.params.size(); ++i)
                    {
                        if (expr.args[i])
                        {
                            PasType argType = typeOf(*expr.args[i]);
                            const PasType &paramType = methodInfo.params[i].second;
                            if (!paramType.isError() && !isAssignableFrom(paramType, argType) && !argType.isError())
                            {
                                error(*expr.args[i], "argument " + std::to_string(i + 1) +
                                                         " type mismatch: expected " + paramType.toString() +
                                                         ", got " + argType.toString());
                            }
                        }
                    }

                    return methodInfo.returnType;
                }
            }

            error(expr, "undefined method '" + calleeName + "' in class '" + className + "'");
            return PasType::unknown();
        }
    }

    // Check argument count (skip for variadic builtins with 0 declared params)
    size_t totalParams = sig->params.size();
    size_t requiredParams = sig->requiredParams;
    size_t actual = expr.args.size();
    bool isVariadic = (totalParams == 0); // Treat 0-param functions as variadic (WriteLn, ReadLn, etc.)

    if (!isVariadic)
    {
        if (actual < requiredParams)
        {
            error(expr, "too few arguments: expected at least " + std::to_string(requiredParams) +
                            ", got " + std::to_string(actual));
        }
        else if (actual > totalParams)
        {
            error(expr, "too many arguments: expected at most " + std::to_string(totalParams) +
                            ", got " + std::to_string(actual));
        }
    }

    // Type-check arguments (for variadic functions, just type-check all args)
    if (isVariadic)
    {
        for (auto &arg : expr.args)
        {
            if (arg)
                typeOf(*arg);
        }
    }
    else
    {
        for (size_t i = 0; i < expr.args.size() && i < sig->params.size(); ++i)
        {
            if (expr.args[i])
            {
                PasType argType = typeOf(*expr.args[i]);
                const PasType &paramType = sig->params[i].second;
                // Skip type check if param is Unknown (used for multi-type builtins like Length)
                if (!paramType.isError() && !isAssignableFrom(paramType, argType) && !argType.isError())
                {
                    error(*expr.args[i], "argument " + std::to_string(i + 1) + " type mismatch: expected " +
                                             paramType.toString() + ", got " + argType.toString());
                }
            }
        }
    }

    // Special validation for SetLength: first argument must be a dynamic array or string
    std::string calleeKey = toLower(calleeName);
    if (calleeKey == "setlength" && !expr.args.empty() && expr.args[0])
    {
        PasType firstArgType = typeOf(*expr.args[0]);
        if (firstArgType.kind == PasTypeKind::Array && firstArgType.dimensions > 0)
        {
            error(*expr.args[0], "SetLength cannot be used on fixed-size arrays");
        }
    }

    return sig->returnType;
}

PasType SemanticAnalyzer::typeOfIndex(IndexExpr &expr)
{
    if (!expr.base)
        return PasType::unknown();

    PasType baseType = typeOf(*expr.base);

    // Check indices
    for (auto &idx : expr.indices)
    {
        if (idx)
        {
            PasType idxType = typeOf(*idx);
            if (!idxType.isOrdinal() && !idxType.isError())
            {
                error(*idx, "array index must be ordinal type");
            }
        }
    }

    // For arrays, return element type
    if (baseType.kind == PasTypeKind::Array && baseType.elementType)
    {
        return *baseType.elementType;
    }

    // For strings, indexing returns a character (also String in Pascal)
    if (baseType.kind == PasTypeKind::String)
    {
        return PasType::string();
    }

    if (!baseType.isError())
    {
        error(expr, "cannot index into " + baseType.toString());
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfField(FieldExpr &expr)
{
    if (!expr.base)
        return PasType::unknown();

    PasType baseType = typeOf(*expr.base);

    // For records/classes, look up field
    if (baseType.kind == PasTypeKind::Record || baseType.kind == PasTypeKind::Class)
    {
        std::string fieldKey = toLower(expr.field);
        auto it = baseType.fields.find(fieldKey);
        if (it != baseType.fields.end() && it->second)
        {
            return *it->second;
        }
        // Field not found - might be a method, allow for now
        return PasType::unknown();
    }

    if (!baseType.isError())
    {
        error(expr, "cannot access field on " + baseType.toString());
    }
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfTypeCast(TypeCastExpr &expr)
{
    if (!expr.targetType)
        return PasType::unknown();

    // Type-check the operand
    if (expr.operand)
        typeOf(*expr.operand);

    return resolveType(*expr.targetType);
}

PasType SemanticAnalyzer::typeOfSetConstructor(SetConstructorExpr &expr)
{
    PasType elemType = PasType::unknown();

    for (auto &elem : expr.elements)
    {
        if (elem.start)
        {
            PasType t = typeOf(*elem.start);
            if (elemType.isError())
                elemType = t;
        }
        if (elem.end)
        {
            typeOf(*elem.end);
        }
    }

    PasType result;
    result.kind = PasTypeKind::Set;
    if (!elemType.isError())
    {
        result.elementType = std::make_shared<PasType>(elemType);
    }
    return result;
}

PasType SemanticAnalyzer::typeOfAddressOf(AddressOfExpr &expr)
{
    // v0.1: Address-of operator is not supported
    error(expr, "address-of operator (@) is not supported in Viper Pascal v0.1; use classes instead");
    return PasType::unknown();
}

PasType SemanticAnalyzer::typeOfDereference(DereferenceExpr &expr)
{
    // v0.1: Pointer dereference is not supported
    error(expr, "pointer dereference (^) is not supported in Viper Pascal v0.1; use classes instead");
    return PasType::unknown();
}

//===----------------------------------------------------------------------===//
// Type Resolution
//===----------------------------------------------------------------------===//

PasType SemanticAnalyzer::resolveType(TypeNode &typeNode)
{
    switch (typeNode.kind)
    {
    case TypeKind::Named: {
        auto &named = static_cast<NamedTypeNode &>(typeNode);
        std::string key = toLower(named.name);

        // Check built-in types first
        if (key == "integer")
            return PasType::integer();
        if (key == "real" || key == "double")
            return PasType::real();
        if (key == "boolean")
            return PasType::boolean();
        if (key == "string")
            return PasType::string();

        // Look up user-defined type
        if (auto type = lookupType(key))
        {
            return *type;
        }

        error(typeNode.loc, "undefined type '" + named.name + "'");
        return PasType::unknown();
    }
    case TypeKind::Optional: {
        auto &opt = static_cast<OptionalTypeNode &>(typeNode);
        if (!opt.inner)
            return PasType::unknown();
        PasType inner = resolveType(*opt.inner);

        // Check for double optional (T??) - this is a compile error
        if (inner.isOptional())
        {
            error(typeNode.loc, "double optional type (T??) is not allowed");
            // Return single optional for error recovery
            return inner;
        }

        return PasType::optional(inner);
    }
    case TypeKind::Array: {
        auto &arr = static_cast<ArrayTypeNode &>(typeNode);
        if (!arr.elementType)
            return PasType::unknown();

        // Validate dimension sizes for fixed arrays
        for (auto &dim : arr.dimensions)
        {
            if (dim.size)
            {
                // Dimension size must be a compile-time constant integer
                if (!isConstantExpr(*dim.size))
                {
                    error(*dim.size, "array dimension must be a compile-time constant");
                    continue;
                }

                // Type-check the dimension expression
                PasType dimType = const_cast<SemanticAnalyzer *>(this)->typeOf(*dim.size);
                if (dimType.kind != PasTypeKind::Integer && dimType.kind != PasTypeKind::Unknown)
                {
                    error(*dim.size, "array dimension must be an integer");
                    continue;
                }

                // Evaluate and check the value is positive
                int64_t dimValue = evaluateConstantInt(*dim.size);
                if (dimValue <= 0)
                {
                    error(*dim.size, "array dimension must be positive");
                }
            }
        }

        PasType elem = resolveType(*arr.elementType);
        return PasType::array(elem, arr.dimensions.size());
    }
    case TypeKind::Record: {
        auto &rec = static_cast<RecordTypeNode &>(typeNode);
        PasType result;
        result.kind = PasTypeKind::Record;
        for (auto &field : rec.fields)
        {
            if (field.type)
            {
                result.fields[toLower(field.name)] =
                    std::make_shared<PasType>(resolveType(*field.type));
            }
        }
        return result;
    }
    case TypeKind::Pointer: {
        // v0.1: Pointer types are not supported
        error(typeNode.loc, "pointer types (^T) are not supported in Viper Pascal v0.1; use classes instead");
        return PasType::unknown();
    }
    case TypeKind::Enum: {
        auto &en = static_cast<EnumTypeNode &>(typeNode);
        return PasType::enumType(en.values);
    }
    case TypeKind::Set: {
        // v0.1: Set types are not supported
        error(typeNode.loc, "set types are not supported in Viper Pascal v0.1");
        return PasType::unknown();
    }
    case TypeKind::Procedure: {
        PasType result;
        result.kind = PasTypeKind::Procedure;
        return result;
    }
    case TypeKind::Function: {
        auto &func = static_cast<FunctionTypeNode &>(typeNode);
        PasType result;
        result.kind = PasTypeKind::Function;
        if (func.returnType)
        {
            result.returnType = std::make_shared<PasType>(resolveType(*func.returnType));
        }
        return result;
    }
    case TypeKind::Range: {
        // Subrange - treat as the base type for now
        PasType result;
        result.kind = PasTypeKind::Range;
        return result;
    }
    }
    return PasType::unknown();
}

bool SemanticAnalyzer::isAssignableFrom(const PasType &target, const PasType &source)
{
    // Unknown types are always assignable (error recovery)
    if (target.isError() || source.isError())
        return true;

    // Nil is assignable to nil-assignable types (check BEFORE optional unwrap)
    if (source.kind == PasTypeKind::Nil && target.isNilAssignable())
        return true;

    // Same kind - check compatibility
    if (target.kind == source.kind)
    {
        // For optional types, also check inner type compatibility
        if (target.kind == PasTypeKind::Optional && target.innerType && source.innerType)
        {
            return isAssignableFrom(*target.innerType, *source.innerType);
        }
        // For classes, check inheritance
        if (target.kind == PasTypeKind::Class)
        {
            return classInheritsFrom(source.name, target.name);
        }
        // For interfaces, check inheritance
        if (target.kind == PasTypeKind::Interface)
        {
            return interfaceExtendsInterface(source.name, target.name);
        }
        // For arrays, check element type compatibility
        if (target.kind == PasTypeKind::Array && target.elementType && source.elementType)
        {
            // Element types must be exactly compatible (not just assignable)
            // Fixed arrays also must have same dimensions
            if (target.dimensions != source.dimensions)
                return false;
            return isAssignableFrom(*target.elementType, *source.elementType);
        }
        return true;
    }

    // T implicitly converts to T? (non-optional to optional)
    if (target.isOptional() && target.innerType)
    {
        return isAssignableFrom(*target.innerType, source);
    }

    // T? does NOT implicitly convert to T (this is the key rule)
    // This is enforced by not having a rule for it

    // Integer can be promoted to Real
    if (target.kind == PasTypeKind::Real && source.kind == PasTypeKind::Integer)
        return true;

    // Integer can be promoted to Real? (optional Real)
    if (target.isOptional() && target.innerType &&
        target.innerType->kind == PasTypeKind::Real && source.kind == PasTypeKind::Integer)
        return true;

    // Enum/Range can be assigned to Integer
    if (target.kind == PasTypeKind::Integer &&
        (source.kind == PasTypeKind::Enum || source.kind == PasTypeKind::Range))
        return true;

    // Class can be assigned to interface if the class implements the interface
    if (target.kind == PasTypeKind::Interface && source.kind == PasTypeKind::Class)
    {
        return classImplementsInterface(source.name, target.name);
    }

    return false;
}

PasType SemanticAnalyzer::binaryResultType(BinaryExpr::Op op, const PasType &left,
                                           const PasType &right)
{
    // Error recovery
    if (left.isError() || right.isError())
        return PasType::unknown();

    switch (op)
    {
    // Arithmetic operators
    case BinaryExpr::Op::Add:
    case BinaryExpr::Op::Sub:
    case BinaryExpr::Op::Mul:
        // String concatenation
        if (left.kind == PasTypeKind::String && right.kind == PasTypeKind::String)
            return PasType::string();
        // Numeric operations
        if (left.isNumeric() && right.isNumeric())
        {
            // Promote to Real if either operand is Real
            if (left.kind == PasTypeKind::Real || right.kind == PasTypeKind::Real)
                return PasType::real();
            return PasType::integer();
        }
        hasError_ = true;
        return PasType::unknown();

    case BinaryExpr::Op::Div:
        // Real division always returns Real
        if (left.isNumeric() && right.isNumeric())
            return PasType::real();
        hasError_ = true;
        return PasType::unknown();

    case BinaryExpr::Op::IntDiv:
    case BinaryExpr::Op::Mod:
        // Integer operations
        if (left.kind == PasTypeKind::Integer && right.kind == PasTypeKind::Integer)
            return PasType::integer();
        hasError_ = true;
        return PasType::unknown();

    // Comparison operators
    case BinaryExpr::Op::Eq:
    case BinaryExpr::Op::Ne:
        // Check for nil comparison with non-optional class/interface
        if ((left.kind == PasTypeKind::Nil &&
             (right.kind == PasTypeKind::Class || right.kind == PasTypeKind::Interface)) ||
            (right.kind == PasTypeKind::Nil &&
             (left.kind == PasTypeKind::Class || left.kind == PasTypeKind::Interface)))
        {
            // Non-optional reference types cannot be compared to nil
            hasError_ = true;
            return PasType::unknown();
        }
        // For enum types, both operands must be the same enum type
        if (left.kind == PasTypeKind::Enum || right.kind == PasTypeKind::Enum)
        {
            if (left.kind != right.kind || left.name != right.name)
            {
                hasError_ = true;
                return PasType::unknown();
            }
        }
        return PasType::boolean();
    case BinaryExpr::Op::Lt:
    case BinaryExpr::Op::Le:
    case BinaryExpr::Op::Gt:
    case BinaryExpr::Op::Ge:
        // For enum types, both operands must be the same enum type
        if (left.kind == PasTypeKind::Enum || right.kind == PasTypeKind::Enum)
        {
            if (left.kind != right.kind || left.name != right.name)
            {
                hasError_ = true;
                return PasType::unknown();
            }
        }
        // Comparisons return Boolean
        return PasType::boolean();

    // Logical operators
    case BinaryExpr::Op::And:
    case BinaryExpr::Op::Or:
        if (left.kind == PasTypeKind::Boolean && right.kind == PasTypeKind::Boolean)
            return PasType::boolean();
        hasError_ = true;
        return PasType::unknown();

    case BinaryExpr::Op::In:
        // Set membership returns Boolean
        return PasType::boolean();

    case BinaryExpr::Op::Coalesce:
        // Nil coalescing (??) operator
        // Rules:
        // - lhs: T?, rhs: T → result: T
        // - lhs: T?, rhs: T? → result: T (unwrap both)
        // - lhs: T, rhs: T → result: T (no-op but valid)
        {
            PasType leftUnwrapped = left.unwrap();
            PasType rightUnwrapped = right.unwrap();

            // Check type compatibility between unwrapped types
            if (!isAssignableFrom(leftUnwrapped, rightUnwrapped) &&
                !isAssignableFrom(rightUnwrapped, leftUnwrapped))
            {
                hasError_ = true;
                return PasType::unknown();
            }

            // Result is always the unwrapped type
            return leftUnwrapped;
        }
    }

    return PasType::unknown();
}

PasType SemanticAnalyzer::unaryResultType(UnaryExpr::Op op, const PasType &operand)
{
    if (operand.isError())
        return PasType::unknown();

    switch (op)
    {
    case UnaryExpr::Op::Neg:
    case UnaryExpr::Op::Plus:
        if (operand.isNumeric())
            return operand;
        hasError_ = true;
        return PasType::unknown();

    case UnaryExpr::Op::Not:
        if (operand.kind == PasTypeKind::Boolean)
            return PasType::boolean();
        hasError_ = true;
        return PasType::unknown();
    }

    return PasType::unknown();
}

bool SemanticAnalyzer::isConstantExpr(const Expr &expr) const
{
    switch (expr.kind)
    {
    case ExprKind::IntLiteral:
    case ExprKind::RealLiteral:
    case ExprKind::StringLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::NilLiteral:
        return true;

    case ExprKind::Name: {
        // Check if it's a constant identifier
        const auto &nameExpr = static_cast<const NameExpr &>(expr);
        std::string key = toLower(nameExpr.name);
        return constants_.count(key) > 0;
    }

    case ExprKind::Unary: {
        // Unary on a constant is still constant
        const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
        return unaryExpr.operand && isConstantExpr(*unaryExpr.operand);
    }

    case ExprKind::Binary: {
        // Binary on constants is still constant (for compile-time evaluable ops)
        const auto &binExpr = static_cast<const BinaryExpr &>(expr);
        return binExpr.left && binExpr.right &&
               isConstantExpr(*binExpr.left) && isConstantExpr(*binExpr.right);
    }

    default:
        return false;
    }
}

int64_t SemanticAnalyzer::evaluateConstantInt(const Expr &expr) const
{
    switch (expr.kind)
    {
    case ExprKind::IntLiteral:
        return static_cast<const IntLiteralExpr &>(expr).value;

    case ExprKind::Name: {
        // Look up constant value
        const auto &nameExpr = static_cast<const NameExpr &>(expr);
        std::string key = toLower(nameExpr.name);

        // Check for stored integer constant value
        auto valIt = constantValues_.find(key);
        if (valIt != constantValues_.end())
        {
            return valIt->second;
        }

        // Check for enum constant with ordinal
        auto it = constants_.find(key);
        if (it != constants_.end())
        {
            if (it->second.kind == PasTypeKind::Enum && it->second.enumOrdinal >= 0)
            {
                return it->second.enumOrdinal;
            }
        }
        return 0;
    }

    case ExprKind::Unary: {
        const auto &unaryExpr = static_cast<const UnaryExpr &>(expr);
        if (!unaryExpr.operand)
            return 0;
        int64_t operand = evaluateConstantInt(*unaryExpr.operand);
        switch (unaryExpr.op)
        {
        case UnaryExpr::Op::Neg:
            return -operand;
        case UnaryExpr::Op::Plus:
            return operand;
        case UnaryExpr::Op::Not:
            return operand ? 0 : 1;
        }
        return 0;
    }

    case ExprKind::Binary: {
        const auto &binExpr = static_cast<const BinaryExpr &>(expr);
        if (!binExpr.left || !binExpr.right)
            return 0;
        int64_t left = evaluateConstantInt(*binExpr.left);
        int64_t right = evaluateConstantInt(*binExpr.right);
        switch (binExpr.op)
        {
        case BinaryExpr::Op::Add:
            return left + right;
        case BinaryExpr::Op::Sub:
            return left - right;
        case BinaryExpr::Op::Mul:
            return left * right;
        case BinaryExpr::Op::IntDiv:
            return right != 0 ? left / right : 0;
        case BinaryExpr::Op::Mod:
            return right != 0 ? left % right : 0;
        default:
            return 0;
        }
    }

    default:
        return 0;
    }
}

size_t SemanticAnalyzer::validateDefaultParams(const std::vector<ParamDecl> &params,
                                                il::support::SourceLoc loc)
{
    bool seenDefault = false;
    size_t requiredCount = 0;

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &param = params[i];

        if (param.defaultValue)
        {
            seenDefault = true;

            // Check that default value is a compile-time constant
            if (!isConstantExpr(*param.defaultValue))
            {
                error(param.loc, "default parameter value must be a compile-time constant");
            }

            // Type-check the default value
            // (Cast away const for typeOf - it doesn't modify the expr semantically)
            PasType defaultType = const_cast<SemanticAnalyzer *>(this)->typeOf(
                *const_cast<Expr *>(param.defaultValue.get()));
            PasType paramType = param.type ? resolveType(*const_cast<TypeNode *>(param.type.get()))
                                           : PasType::unknown();

            if (!isAssignableFrom(paramType, defaultType) && !defaultType.isError())
            {
                error(param.loc, "default value type " + defaultType.toString() +
                                     " is not compatible with parameter type " + paramType.toString());
            }
        }
        else if (seenDefault)
        {
            // Error: non-default parameter after default parameter
            error(param.loc, "parameter '" + param.name +
                                 "' must have a default value because it follows a parameter with a default");
        }
        else
        {
            requiredCount++;
        }
    }

    return requiredCount;
}

//===----------------------------------------------------------------------===//
// Scope Management
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::pushScope()
{
    varScopes_.emplace_back();
}

void SemanticAnalyzer::popScope()
{
    if (!varScopes_.empty())
    {
        // Clear any tracking for variables in this scope
        for (const auto &pair : varScopes_.back())
        {
            uninitializedNonNullableVars_.erase(pair.first);
            definitelyAssignedVars_.erase(pair.first);
        }
        varScopes_.pop_back();
    }
}

void SemanticAnalyzer::addVariable(const std::string &name, const PasType &type)
{
    if (!varScopes_.empty())
    {
        varScopes_.back()[name] = type;
    }
}

void SemanticAnalyzer::addLocalVariable(const std::string &name, const PasType &type)
{
    addVariable(name, type);

    // Track non-nullable reference types that require definite assignment
    if (type.requiresDefiniteAssignment())
    {
        uninitializedNonNullableVars_.insert(name);
    }
}

void SemanticAnalyzer::markDefinitelyAssigned(const std::string &name)
{
    std::string key = toLower(name);
    uninitializedNonNullableVars_.erase(key);
    definitelyAssignedVars_.insert(key);
}

bool SemanticAnalyzer::isDefinitelyAssigned(const std::string &name) const
{
    std::string key = toLower(name);
    // If not in the uninitialized set, it's either:
    // 1. Not a non-nullable reference type
    // 2. Definitely assigned
    return uninitializedNonNullableVars_.count(key) == 0;
}

//===----------------------------------------------------------------------===//
// Lookup Functions
//===----------------------------------------------------------------------===//

std::optional<PasType> SemanticAnalyzer::lookupType(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = types_.find(key);
    if (it != types_.end())
        return it->second;
    return std::nullopt;
}

std::optional<PasType> SemanticAnalyzer::lookupVariable(const std::string &name) const
{
    std::string key = toLower(name);
    // Search from innermost scope outward
    for (auto it = varScopes_.rbegin(); it != varScopes_.rend(); ++it)
    {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }
    return std::nullopt;
}

std::optional<PasType> SemanticAnalyzer::lookupConstant(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = constants_.find(key);
    if (it != constants_.end())
        return it->second;
    return std::nullopt;
}

const FuncSignature *SemanticAnalyzer::lookupFunction(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = functions_.find(key);
    if (it != functions_.end())
        return &it->second;
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::error(il::support::SourceLoc loc, const std::string &message)
{
    hasError_ = true;
    diag_.report({il::support::Severity::Error, message, loc, ""});
}

void SemanticAnalyzer::error(const Expr &expr, const std::string &message)
{
    error(expr.loc, message);
}

void SemanticAnalyzer::error(const Stmt &stmt, const std::string &message)
{
    error(stmt.loc, message);
}

//===----------------------------------------------------------------------===//
// Built-in Registration
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::registerPrimitives()
{
    types_["integer"] = PasType::integer();
    types_["real"] = PasType::real();
    types_["double"] = PasType::real();
    types_["boolean"] = PasType::boolean();
    types_["string"] = PasType::string();

    // Register Exception as predefined class type
    types_["exception"] = PasType::classType("Exception");

    // Register Exception class info for inheritance checking
    ClassInfo excInfo;
    excInfo.name = "Exception";
    excInfo.baseClass = "";
    excInfo.hasConstructor = true;
    // Add Message property (read-only string)
    FieldInfo msgField;
    msgField.name = "Message";
    msgField.type = PasType::string();
    msgField.visibility = Visibility::Public;
    excInfo.fields["message"] = msgField;
    classes_["exception"] = excInfo;
}

//===----------------------------------------------------------------------===//
// Flow-Sensitive Narrowing
//===----------------------------------------------------------------------===//

bool SemanticAnalyzer::isNilCheck(const Expr &expr, std::string &varName, bool &isNotNil) const
{
    // Check if expr is a binary comparison with nil
    if (expr.kind != ExprKind::Binary)
        return false;

    const auto &binExpr = static_cast<const BinaryExpr &>(expr);

    // Must be = or <> comparison
    if (binExpr.op != BinaryExpr::Op::Eq && binExpr.op != BinaryExpr::Op::Ne)
        return false;

    // One side must be nil, the other a simple name
    const Expr *nameExpr = nullptr;
    const Expr *nilExpr = nullptr;

    if (binExpr.left && binExpr.left->kind == ExprKind::Name &&
        binExpr.right && binExpr.right->kind == ExprKind::NilLiteral)
    {
        nameExpr = binExpr.left.get();
        nilExpr = binExpr.right.get();
    }
    else if (binExpr.left && binExpr.left->kind == ExprKind::NilLiteral &&
             binExpr.right && binExpr.right->kind == ExprKind::Name)
    {
        nilExpr = binExpr.left.get();
        nameExpr = binExpr.right.get();
    }
    else
    {
        return false;
    }

    varName = toLower(static_cast<const NameExpr *>(nameExpr)->name);
    isNotNil = (binExpr.op == BinaryExpr::Op::Ne); // <> nil means "is not nil"
    return true;
}

void SemanticAnalyzer::pushNarrowing(const std::map<std::string, PasType> &narrowed)
{
    narrowingScopes_.push_back(narrowed);
}

void SemanticAnalyzer::popNarrowing()
{
    if (!narrowingScopes_.empty())
        narrowingScopes_.pop_back();
}

void SemanticAnalyzer::invalidateNarrowing(const std::string &varName)
{
    std::string key = toLower(varName);
    // Remove from all narrowing scopes
    for (auto &scope : narrowingScopes_)
    {
        scope.erase(key);
    }
}

std::optional<PasType> SemanticAnalyzer::lookupEffectiveType(const std::string &name) const
{
    std::string key = toLower(name);

    // Check narrowing scopes first (from innermost to outermost)
    for (auto it = narrowingScopes_.rbegin(); it != narrowingScopes_.rend(); ++it)
    {
        auto found = it->find(key);
        if (found != it->end())
            return found->second;
    }

    // Fall back to regular variable lookup
    return lookupVariable(key);
}

void SemanticAnalyzer::registerBuiltins()
{
    // Helper to map ResultKind to PasType
    auto resultTypeToPassType = [](ResultKind kind) -> PasType {
        switch (kind)
        {
        case ResultKind::Void:
            return PasType::voidType();
        case ResultKind::Integer:
            return PasType::integer();
        case ResultKind::Real:
            return PasType::real();
        case ResultKind::String:
            return PasType::string();
        case ResultKind::Boolean:
            return PasType::boolean();
        case ResultKind::FromArg:
            // Default to Integer for FromArg builtins
            return PasType::integer();
        }
        return PasType::unknown();
    };

    // Helper to map ArgTypeMask to PasType (for signature purposes)
    // For Numeric, prefer Real since integers auto-promote to real
    // For multi-type args (String|Array), return an Any type marker
    auto maskToType = [](ArgTypeMask mask) -> PasType {
        // For Numeric (both Int + Real allowed), use Real to allow promotion
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Real))
            return PasType::real();
        // Ordinal includes Integer and Boolean - use Integer for compatibility
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Boolean))
            return PasType::integer(); // Ordinal - accept integers (booleans are ordinal too)
        // String|Array - mark as Any so type checking is deferred
        if ((mask & ArgTypeMask::String) && (mask & ArgTypeMask::Array))
            return PasType::unknown(); // Use unknown as "Any" marker
        // Otherwise, order of preference: Integer, Real, String, Boolean, Array
        if (mask & ArgTypeMask::Integer)
            return PasType::integer();
        if (mask & ArgTypeMask::Real)
            return PasType::real();
        if (mask & ArgTypeMask::String)
            return PasType::string();
        if (mask & ArgTypeMask::Boolean)
            return PasType::boolean();
        if (mask & ArgTypeMask::Array)
            return PasType::array(PasType::unknown(), 0); // Generic array type
        return PasType::unknown();
    };

    // Register all builtins from the registry (only core builtins, not unit-specific)
    for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
    {
        auto id = static_cast<PascalBuiltin>(i);
        const auto &desc = getBuiltinDescriptor(id);

        // Skip if no name (unused entry)
        if (!desc.name)
            continue;

        // Skip Viper unit builtins (they require uses clause)
        if (desc.category != BuiltinCategory::Builtin)
            continue;

        FuncSignature sig;
        sig.name = desc.name;
        sig.returnType = resultTypeToPassType(desc.result);

        // For variadic builtins, leave params empty (checked specially in typeOfCall)
        if (!desc.variadic)
        {
            for (const auto &arg : desc.args)
            {
                if (!arg.optional)
                {
                    sig.params.emplace_back("arg", maskToType(arg.allowed));
                    sig.isVarParam.push_back(arg.isVar);
                }
            }
        }

        functions_[toLower(desc.name)] = sig;
    }

    // Register built-in units (Viper.Strings, Viper.Math)
    registerBuiltinUnits();
}

void SemanticAnalyzer::registerBuiltinUnits()
{
    // Helper to map ResultKind to PasType
    auto resultTypeToPassType = [](ResultKind kind) -> PasType {
        switch (kind)
        {
        case ResultKind::Void:
            return PasType::voidType();
        case ResultKind::Integer:
            return PasType::integer();
        case ResultKind::Real:
            return PasType::real();
        case ResultKind::String:
            return PasType::string();
        case ResultKind::Boolean:
            return PasType::boolean();
        case ResultKind::FromArg:
            return PasType::integer();
        }
        return PasType::unknown();
    };

    // Helper to map ArgTypeMask to PasType
    auto maskToType = [](ArgTypeMask mask) -> PasType {
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Real))
            return PasType::real();
        if ((mask & ArgTypeMask::Integer) && (mask & ArgTypeMask::Boolean))
            return PasType::integer();
        if ((mask & ArgTypeMask::String) && (mask & ArgTypeMask::Array))
            return PasType::unknown();
        if (mask & ArgTypeMask::Integer)
            return PasType::integer();
        if (mask & ArgTypeMask::Real)
            return PasType::real();
        if (mask & ArgTypeMask::String)
            return PasType::string();
        if (mask & ArgTypeMask::Boolean)
            return PasType::boolean();
        if (mask & ArgTypeMask::Array)
            return PasType::array(PasType::unknown(), 0);
        return PasType::unknown();
    };

    //=========================================================================
    // Viper.Strings Unit
    //=========================================================================
    {
        UnitInfo unit;
        unit.name = "Viper.Strings";

        // Register all Viper.Strings builtins
        for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
        {
            auto id = static_cast<PascalBuiltin>(i);
            const auto &desc = getBuiltinDescriptor(id);

            if (!desc.name || desc.category != BuiltinCategory::ViperStrings)
                continue;

            FuncSignature sig;
            sig.name = desc.name;
            sig.returnType = resultTypeToPassType(desc.result);

            if (!desc.variadic)
            {
                for (const auto &arg : desc.args)
                {
                    if (!arg.optional)
                    {
                        sig.params.emplace_back("arg", maskToType(arg.allowed));
                        sig.isVarParam.push_back(arg.isVar);
                    }
                }
            }

            unit.functions[toLower(desc.name)] = sig;
        }

        registerUnit(unit);
    }

    //=========================================================================
    // Viper.Math Unit
    //=========================================================================
    {
        UnitInfo unit;
        unit.name = "Viper.Math";

        // Register constants Pi and E
        PasType piConst = PasType::real();
        PasType eConst = PasType::real();
        unit.constants["pi"] = piConst;
        unit.constants["e"] = eConst;

        // Register all Viper.Math builtins
        for (size_t i = 0; i < static_cast<size_t>(PascalBuiltin::Count); ++i)
        {
            auto id = static_cast<PascalBuiltin>(i);
            const auto &desc = getBuiltinDescriptor(id);

            if (!desc.name || desc.category != BuiltinCategory::ViperMath)
                continue;

            FuncSignature sig;
            sig.name = desc.name;
            sig.returnType = resultTypeToPassType(desc.result);

            if (!desc.variadic)
            {
                for (const auto &arg : desc.args)
                {
                    if (!arg.optional)
                    {
                        sig.params.emplace_back("arg", maskToType(arg.allowed));
                        sig.isVarParam.push_back(arg.isVar);
                    }
                }
            }

            unit.functions[toLower(desc.name)] = sig;
        }

        // Also register core math functions that should be in the unit per spec:
        // Sqrt, Abs, Floor, Ceil, Sin, Cos, Tan, Exp, Ln (already in core)
        // These are available in core, but the unit re-exports them for consistency
        auto addCoreFunc = [&](const char *name, ResultKind res, ArgTypeMask argMask) {
            FuncSignature sig;
            sig.name = name;
            sig.returnType = resultTypeToPassType(res);
            sig.params.emplace_back("arg", maskToType(argMask));
            sig.isVarParam.push_back(false);
            unit.functions[toLower(name)] = sig;
        };

        // Re-export core math functions in the unit
        addCoreFunc("Sqrt", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Abs", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Floor", ResultKind::Integer, ArgTypeMask::Numeric);
        addCoreFunc("Ceil", ResultKind::Integer, ArgTypeMask::Numeric);
        addCoreFunc("Sin", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Cos", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Tan", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Exp", ResultKind::Real, ArgTypeMask::Numeric);
        addCoreFunc("Ln", ResultKind::Real, ArgTypeMask::Numeric);

        registerUnit(unit);
    }
}

//===----------------------------------------------------------------------===//
// Unit Management
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::registerUnit(const UnitInfo &unitInfo)
{
    std::string key = toLower(unitInfo.name);
    units_[key] = unitInfo;
}

const UnitInfo *SemanticAnalyzer::getUnit(const std::string &name) const
{
    auto it = units_.find(toLower(name));
    if (it != units_.end())
        return &it->second;
    return nullptr;
}

bool SemanticAnalyzer::importUnits(const std::vector<std::string> &unitNames)
{
    bool allFound = true;

    for (const auto &unitName : unitNames)
    {
        // Check if this is a Viper standard unit (Viper.Strings, Viper.Math, Crt, etc.)
        if (isViperUnit(unitName))
        {
            // Import builtins from this Viper unit
            auto builtins = getUnitBuiltins(unitName);
            for (auto builtin : builtins)
            {
                const auto &desc = getBuiltinDescriptor(builtin);
                if (!desc.name)
                    continue;

                // Register the builtin as a function
                FuncSignature sig;
                sig.name = desc.name;

                // Set up parameters
                for (size_t i = 0; i < desc.args.size() && i < desc.maxArgs; ++i)
                {
                    const auto &argSpec = desc.args[i];
                    PasType paramType; // default to unknown

                    // Determine parameter type from allowed mask
                    if (argSpec.allowed & ArgTypeMask::Integer)
                        paramType = PasType::integer();
                    else if (argSpec.allowed & ArgTypeMask::Real)
                        paramType = PasType::real();
                    else if (argSpec.allowed & ArgTypeMask::String)
                        paramType = PasType::string();
                    else if (argSpec.allowed & ArgTypeMask::Boolean)
                        paramType = PasType::boolean();

                    sig.params.push_back({"arg" + std::to_string(i), paramType});
                    sig.isVarParam.push_back(argSpec.isVar);
                    sig.hasDefault.push_back(argSpec.optional);
                }

                // Set return type
                sig.returnType = getBuiltinResultType(builtin);
                sig.requiredParams = desc.minArgs;

                std::string key = toLower(desc.name);
                functions_[key] = sig;
            }
            continue;
        }

        const UnitInfo *unit = getUnit(unitName);
        if (!unit)
        {
            error({}, "unit '" + unitName + "' not found");
            allFound = false;
            continue;
        }

        // Import all exported symbols into the current scope
        for (const auto &[key, type] : unit->types)
        {
            types_[key] = type;
        }

        for (const auto &[key, constType] : unit->constants)
        {
            constants_[key] = constType;
        }

        for (const auto &[key, sig] : unit->functions)
        {
            functions_[key] = sig;
        }

        for (const auto &[key, classInfo] : unit->classes)
        {
            classes_[key] = classInfo;
        }

        for (const auto &[key, ifaceInfo] : unit->interfaces)
        {
            interfaces_[key] = ifaceInfo;
        }
    }

    return allFound;
}

UnitInfo SemanticAnalyzer::extractUnitExports(const Unit &unit)
{
    UnitInfo info;
    info.name = unit.name;

    // Process each interface declaration
    for (const auto &decl : unit.interfaceDecls)
    {
        if (!decl)
            continue;

        switch (decl->kind)
        {
        case DeclKind::Type: {
            auto &td = static_cast<const TypeDecl &>(*decl);
            std::string key = toLower(td.name);
            // Type must be resolved - look it up from current types_
            auto it = types_.find(key);
            if (it != types_.end())
                info.types[key] = it->second;
            break;
        }
        case DeclKind::Const: {
            auto &cd = static_cast<const ConstDecl &>(*decl);
            std::string key = toLower(cd.name);
            auto it = constants_.find(key);
            if (it != constants_.end())
                info.constants[key] = it->second;
            break;
        }
        case DeclKind::Procedure:
        case DeclKind::Function: {
            std::string name;
            if (decl->kind == DeclKind::Procedure)
                name = static_cast<const ProcedureDecl &>(*decl).name;
            else
                name = static_cast<const FunctionDecl &>(*decl).name;
            std::string key = toLower(name);
            auto it = functions_.find(key);
            if (it != functions_.end())
                info.functions[key] = it->second;
            break;
        }
        case DeclKind::Class: {
            auto &cd = static_cast<const ClassDecl &>(*decl);
            std::string key = toLower(cd.name);
            auto it = classes_.find(key);
            if (it != classes_.end())
                info.classes[key] = it->second;
            break;
        }
        case DeclKind::Interface: {
            auto &id = static_cast<const InterfaceDecl &>(*decl);
            std::string key = toLower(id.name);
            auto it = interfaces_.find(key);
            if (it != interfaces_.end())
                info.interfaces[key] = it->second;
            break;
        }
        default:
            break;
        }
    }

    return info;
}

} // namespace il::frontends::pascal
