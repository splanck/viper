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
#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

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

    // Handle namespace binds (e.g., "Viper.Terminal")
    if (decl.isNamespaceBind)
    {
        analyzeNamespaceBind(decl);
        return;
    }

    // Handle file binds (existing logic)
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
        // Remove directory components (handle both / and \ separators)
        auto lastSlash = path.find_last_of("/\\");
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

/// @brief Analyze a bind declaration that imports a runtime namespace.
/// @details Handles three forms of namespace binding:
///          1. Selective import: `bind Viper.Terminal { Say, ReadLine };`
///             - Only listed symbols are imported into scope
///          2. Alias import: `bind Viper.Terminal as T;`
///             - Namespace accessible via alias (e.g., T.Say())
///          3. Full import: `bind Viper.Terminal;`
///             - All namespace symbols imported into current scope
///          Validates that the namespace exists and checks for symbol conflicts.
/// @param decl The bind declaration AST node to analyze.
void Sema::analyzeNamespaceBind(BindDecl &decl)
{
    const std::string &ns = decl.path;

    // Validate this is a known runtime namespace
    if (!isValidRuntimeNamespace(ns))
    {
        error(decl.loc, "Unknown runtime namespace: " + ns);
        return;
    }

    // Store the bound namespace
    boundNamespaces_[ns] = decl.alias;

    if (!decl.specificItems.empty())
    {
        // Selective import: bind Viper.Terminal { Say, ReadLine };
        for (const auto &item : decl.specificItems)
        {
            std::string fullName = ns + "." + item;
            Symbol *sym = lookupSymbol(fullName);
            if (!sym)
            {
                error(decl.loc, "Unknown symbol '" + item + "' in namespace " + ns);
                continue;
            }
            // Check for conflicts with existing imports
            auto existingIt = importedSymbols_.find(item);
            if (existingIt != importedSymbols_.end() && existingIt->second != fullName)
            {
                error(decl.loc,
                      "Symbol '" + item + "' conflicts with existing import from " +
                          existingIt->second);
                continue;
            }
            importedSymbols_[item] = fullName;
        }
    }
    else if (!decl.alias.empty())
    {
        // Alias import: bind Viper.Terminal as T;
        // Register alias as a module symbol for qualified access
        Symbol sym;
        sym.kind = Symbol::Kind::Module;
        sym.name = decl.alias;
        sym.type = types::module(ns);
        sym.isFinal = true;
        defineSymbol(decl.alias, sym);
        // Also register in importedSymbols_ so type resolution can expand
        // aliased dotted names (e.g., T.Canvas → Viper.Graphics.Canvas)
        importedSymbols_[decl.alias] = ns;
    }
    else
    {
        // Full namespace import: bind Viper.Terminal;
        // Import all symbols from this namespace into scope
        importNamespaceSymbols(ns);
    }
}

bool Sema::isValidRuntimeNamespace(const std::string &ns)
{
    // A namespace is valid if any runtime class or method starts with "ns."
    const std::string prefix = ns + ".";

    // Check typeRegistry_ for runtime class types
    for (const auto &[name, type] : typeRegistry_)
    {
        if (name.rfind(prefix, 0) == 0 || name == ns)
        {
            return true;
        }
    }

    // Also check the RuntimeRegistry for methods that match this namespace
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    const auto &catalog = registry.rawCatalog();
    for (const auto &cls : catalog)
    {
        // Check if class is in this namespace
        std::string clsName = cls.qname ? cls.qname : "";
        if (clsName.rfind(prefix, 0) == 0 || clsName == ns)
            return true;

        // Check if any method target starts with this namespace
        for (const auto &m : cls.methods)
        {
            if (m.target && std::string(m.target).rfind(prefix, 0) == 0)
                return true;
        }
    }

    // Check scope symbols for extern functions registered via defineExternFunction
    // (e.g., Viper.Box.* functions that aren't part of a runtime class)
    for (Scope *s = currentScope_; s != nullptr; s = s->parent())
    {
        if (s->hasSymbolWithPrefix(prefix))
            return true;
    }

    return false;
}

void Sema::importNamespaceSymbols(const std::string &ns)
{
    // Import all symbols from this namespace
    const std::string prefix = ns + ".";

    // Walk through all registered types and import matching class names
    for (const auto &[name, type] : typeRegistry_)
    {
        if (name.rfind(prefix, 0) == 0)
        {
            // Extract short name (e.g., "Canvas" from "Viper.Graphics.Canvas")
            std::string shortName = name.substr(prefix.size());
            // Skip nested namespaces (only import direct children)
            if (shortName.find('.') != std::string::npos)
                continue;

            // Check for conflicts
            auto existingIt = importedSymbols_.find(shortName);
            if (existingIt != importedSymbols_.end() && existingIt->second != name)
            {
                // Conflict - first import wins, but we could warn here
                continue;
            }
            importedSymbols_[shortName] = name;
        }
    }

    // Import class names and function symbols using the RuntimeRegistry
    // This gives us access to all registered runtime classes and methods
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    const auto &catalog = registry.rawCatalog();

    for (const auto &cls : catalog)
    {
        // Import the class name itself (e.g., "Canvas" from "Viper.Graphics.Canvas")
        std::string clsName = cls.qname ? cls.qname : "";
        if (!clsName.empty() && clsName.rfind(prefix, 0) == 0)
        {
            // Extract short name
            std::string shortName = clsName.substr(prefix.size());
            // Skip nested namespaces (only import direct children)
            if (shortName.find('.') == std::string::npos)
            {
                auto existingIt = importedSymbols_.find(shortName);
                if (existingIt == importedSymbols_.end())
                {
                    importedSymbols_[shortName] = clsName;
                }
            }
        }

        // Import methods from classes in this namespace
        for (const auto &m : cls.methods)
        {
            if (!m.target)
                continue;

            std::string target(m.target);
            if (target.rfind(prefix, 0) != 0)
                continue;

            // Extract short name (e.g., "Say" from "Viper.Terminal.Say")
            std::string shortName = target.substr(prefix.size());

            // Skip nested namespaces (only import direct children)
            if (shortName.find('.') != std::string::npos)
                continue;

            // Check for conflicts
            auto existingIt = importedSymbols_.find(shortName);
            if (existingIt != importedSymbols_.end() && existingIt->second != target)
            {
                // Conflict - first import wins
                continue;
            }
            importedSymbols_[shortName] = target;
        }

        // Also import properties by their display name (e.g., "Length")
        // mapped to the getter's qualified name (e.g., "Viper.String.get_Length")
        for (const auto &p : cls.properties)
        {
            if (p.getter && p.name)
            {
                std::string getter(p.getter);
                if (getter.rfind(prefix, 0) != 0)
                    continue;

                std::string shortName(p.name);
                if (shortName.find('.') != std::string::npos)
                    continue;

                auto existingIt = importedSymbols_.find(shortName);
                if (existingIt == importedSymbols_.end())
                    importedSymbols_[shortName] = getter;
            }
        }

    }

    // Discover sub-namespace prefixes from standalone runtime functions
    // (registered via runtime.def, not in the RuntimeClasses catalog).
    // For example, Viper.GUI.Shortcuts.Register → register "Shortcuts"
    // as a module-like symbol mapping to "Viper.GUI.Shortcuts".
    for (const auto &alias : il::runtime::kRuntimeNameAliases)
    {
        std::string canonical(alias.canonical);
        if (canonical.rfind(prefix, 0) != 0)
            continue;
        std::string shortName = canonical.substr(prefix.size());
        auto dotPos = shortName.find('.');
        if (dotPos == std::string::npos)
            continue; // Direct child, already handled above
        std::string subNs = shortName.substr(0, dotPos);
        if (importedSymbols_.find(subNs) == importedSymbols_.end())
            importedSymbols_[subNs] = ns + "." + subNs;
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

void Sema::validateInterfaceImplementations(const std::string &typeName,
                                            const SourceLoc &loc,
                                            const std::vector<std::string> &interfaces)
{
    for (const auto &ifaceName : interfaces)
    {
        auto ifaceIt = interfaceDecls_.find(ifaceName);
        if (ifaceIt == interfaceDecls_.end())
        {
            error(loc, "Unknown interface: " + ifaceName);
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

            std::string implKey = typeName + "." + ifaceMethod->name;
            auto implIt = methodTypes_.find(implKey);
            if (implIt == methodTypes_.end())
            {
                error(loc,
                      "Type '" + typeName + "' does not implement interface method '" + ifaceName +
                          "." + ifaceMethod->name + "'");
                ok = false;
                continue;
            }

            if (!implIt->second->equals(*ifaceTypeIt->second))
            {
                error(loc,
                      "Method '" + typeName + "." + ifaceMethod->name +
                          "' does not match interface '" + ifaceName + "." + ifaceMethod->name +
                          "' signature");
                ok = false;
            }

            auto visIt = memberVisibility_.find(implKey);
            if (visIt != memberVisibility_.end() && visIt->second != Visibility::Public)
            {
                error(loc,
                      "Method '" + typeName + "." + ifaceMethod->name +
                          "' must be public to satisfy interface '" + ifaceName + "'");
                ok = false;
            }
        }

        if (ok)
            types::registerInterfaceImplementation(typeName, ifaceName);
    }
}

void Sema::analyzeValueDecl(ValueDecl &decl)
{
    // Generic types are registered in the first pass; skip body analysis
    if (!decl.genericParams.empty())
        return;

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
    validateInterfaceImplementations(decl.name, decl.loc, decl.interfaces);

    popScope();
    currentSelfType_ = nullptr;
}

template <typename T> void Sema::registerTypeMembers(T &decl, bool includeFields)
{
    // Register field types (if applicable)
    if (includeFields)
    {
        for (auto &member : decl.members)
        {
            if (member->kind == DeclKind::Field)
            {
                auto *field = static_cast<FieldDecl *>(member.get());
                TypeRef fieldType =
                    field->type ? resolveTypeNode(field->type.get()) : types::unknown();
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
    // Skip member registration for generic types - done during instantiation
    if (!decl.genericParams.empty())
        return;
    registerTypeMembers(decl, true);
}

void Sema::registerValueMembers(ValueDecl &decl)
{
    // Skip member registration for generic types - done during instantiation
    if (!decl.genericParams.empty())
        return;
    registerTypeMembers(decl, true);
}

void Sema::registerInterfaceMembers(InterfaceDecl &decl)
{
    registerTypeMembers(decl, false);
}

void Sema::analyzeEntityDecl(EntityDecl &decl)
{
    // Generic types are registered in the first pass; skip body analysis
    if (!decl.genericParams.empty())
        return;

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

            // Add ALL parent's fields to this entity's scope (including inherited fields)
            // by scanning fieldTypes_ for entries with parent's name prefix.
            // This ensures grandparent fields are also accessible in grandchildren.
            std::string parentPrefix = parent->name + ".";
            for (const auto &entry : fieldTypes_)
            {
                if (entry.first.rfind(parentPrefix, 0) == 0)
                {
                    // Extract field name from "Parent.fieldName"
                    std::string fieldName = entry.first.substr(parentPrefix.size());
                    // Skip if already defined (could be overridden)
                    if (!currentScope_->lookupLocal(fieldName))
                    {
                        Symbol sym;
                        sym.kind = Symbol::Kind::Field;
                        sym.name = fieldName;
                        sym.type = entry.second;
                        defineSymbol(fieldName, sym);
                        // Also register in this entity's field types
                        fieldTypes_[decl.name + "." + fieldName] = entry.second;
                    }
                }
            }

            // Add ALL parent's methods to this entity's scope (including inherited methods)
            std::string methodPrefix = parent->name + ".";
            for (const auto &entry : methodTypes_)
            {
                if (entry.first.rfind(methodPrefix, 0) == 0)
                {
                    // Extract method name from "Parent.methodName"
                    std::string methodName = entry.first.substr(methodPrefix.size());
                    // Skip if already defined (could be overridden)
                    if (!currentScope_->lookupLocal(methodName))
                    {
                        Symbol sym;
                        sym.kind = Symbol::Kind::Method;
                        sym.name = methodName;
                        sym.type = entry.second;
                        sym.isFinal = true;
                        defineSymbol(methodName, sym);
                        // Also register in this entity's method types
                        methodTypes_[decl.name + "." + methodName] = entry.second;
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

    // Validate interface implementations
    validateInterfaceImplementations(decl.name, decl.loc, decl.interfaces);

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
    // Generic functions are registered in the first pass; skip body analysis
    // The body will be analyzed when the function is instantiated
    if (!decl.genericParams.empty())
        return;

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
