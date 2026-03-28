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

#include <unordered_set>

namespace il::frontends::zia {

//=============================================================================
// Declaration Analysis
//=============================================================================

/// @brief Analyze a bind declaration (file import or namespace bind).
/// @details Routes to analyzeNamespaceBind() for namespace paths, or registers
///          the file module name as a Module symbol for qualified access.
/// @param decl The bind declaration to analyze.
void Sema::analyzeBind(BindDecl &decl) {
    if (decl.path.empty()) {
        error(decl.loc, "Bind path cannot be empty");
        return;
    }

    // Handle namespace binds (e.g., "Viper.Terminal")
    if (decl.isNamespaceBind) {
        analyzeNamespaceBind(decl);
        return;
    }

    // W012: Duplicate import
    if (binds_.count(decl.path)) {
        warn(
            WarningCode::W012_DuplicateImport, decl.loc, "Duplicate import of '" + decl.path + "'");
    }

    // Handle file binds (existing logic)
    binds_.insert(decl.path);

    // Extract module name from bind path
    // For "./colors" or "../utils/colors", extract "colors"
    // For "colors", use "colors"
    std::string moduleName;
    if (!decl.alias.empty()) {
        // Use alias if provided: bind "./colors" as c;
        moduleName = decl.alias;
    } else {
        // Extract filename without extension from path
        std::string path = decl.path;
        // Remove directory components (handle both / and \ separators)
        auto lastSlash = path.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            path = path.substr(lastSlash + 1);
        }
        // Remove .zia extension if present
        auto extPos = path.rfind(".zia");
        if (extPos != std::string::npos) {
            path = path.substr(0, extPos);
        }
        moduleName = path;
    }

    // Register the module name as a Module symbol for qualified access
    if (!moduleName.empty()) {
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
void Sema::analyzeNamespaceBind(BindDecl &decl) {
    const std::string &ns = decl.path;

    // Validate this is a known runtime namespace
    if (!isValidRuntimeNamespace(ns)) {
        error(decl.loc, "Unknown runtime namespace: " + ns);
        return;
    }

    // Store the bound namespace
    boundNamespaces_[ns] = decl.alias;
    if (!decl.alias.empty())
        aliasToNamespace_[decl.alias] = ns;

    if (!decl.specificItems.empty()) {
        // Selective import: bind Viper.Terminal { Say, ReadLine };
        for (const auto &item : decl.specificItems) {
            std::string fullName = ns + "." + item;
            Symbol *sym = lookupSymbol(fullName);
            if (!sym) {
                error(decl.loc, "Unknown symbol '" + item + "' in namespace " + ns);
                continue;
            }
            // Check for conflicts with existing imports
            auto existingIt = importedSymbols_.find(item);
            if (existingIt != importedSymbols_.end() && existingIt->second != fullName) {
                error(decl.loc,
                      "Symbol '" + item + "' conflicts with existing import from " +
                          existingIt->second);
                continue;
            }
            importedSymbols_[item] = fullName;
        }
    } else if (!decl.alias.empty()) {
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
    } else {
        // Full namespace import: bind Viper.Terminal;
        // Import all symbols from this namespace into scope
        importNamespaceSymbols(ns);
    }
}

/// @brief Check whether a runtime namespace exists.
/// @details Scans type registry, runtime class catalog, and scope symbols
///          for any entry matching the given namespace prefix.
/// @param ns The namespace string to validate.
/// @return True if any registered type, class, or method matches the namespace prefix.
bool Sema::isValidRuntimeNamespace(const std::string &ns) {
    // A namespace is valid if any runtime class or method starts with "ns."
    const std::string prefix = ns + ".";

    // Check typeRegistry_ for runtime class types
    for (const auto &[name, type] : typeRegistry_) {
        if (name.rfind(prefix, 0) == 0 || name == ns) {
            return true;
        }
    }

    // Also check the RuntimeRegistry for methods that match this namespace
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    const auto &catalog = registry.rawCatalog();
    for (const auto &cls : catalog) {
        // Check if class is in this namespace
        std::string clsName = cls.qname ? cls.qname : "";
        if (clsName.rfind(prefix, 0) == 0 || clsName == ns)
            return true;

        // Check if any method target starts with this namespace
        for (const auto &m : cls.methods) {
            if (m.target && std::string(m.target).rfind(prefix, 0) == 0)
                return true;
        }
    }

    // Check scope symbols for extern functions registered via defineExternFunction
    // (e.g., Viper.Box.* functions that aren't part of a runtime class)
    for (Scope *s = currentScope_; s != nullptr; s = s->parent()) {
        if (s->hasSymbolWithPrefix(prefix))
            return true;
    }

    return false;
}

/// @brief Import all symbols from a runtime namespace into the current scope.
/// @details Imports types from typeRegistry_, classes and methods from RuntimeRegistry,
///          properties by display name, and sub-namespace prefixes from kRuntimeNameAliases.
/// @param ns The namespace whose symbols should be imported.
void Sema::importNamespaceSymbols(const std::string &ns) {
    // Import all symbols from this namespace
    const std::string prefix = ns + ".";

    // Walk through all registered types and import matching class names
    for (const auto &[name, type] : typeRegistry_) {
        if (name.rfind(prefix, 0) == 0) {
            // Extract short name (e.g., "Canvas" from "Viper.Graphics.Canvas")
            std::string shortName = name.substr(prefix.size());
            // Skip nested namespaces (only import direct children)
            if (shortName.find('.') != std::string::npos)
                continue;

            // Check for conflicts
            auto existingIt = importedSymbols_.find(shortName);
            if (existingIt != importedSymbols_.end() && existingIt->second != name) {
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

    for (const auto &cls : catalog) {
        // Import the class name itself (e.g., "Canvas" from "Viper.Graphics.Canvas")
        std::string clsName = cls.qname ? cls.qname : "";
        if (!clsName.empty() && clsName.rfind(prefix, 0) == 0) {
            // Extract short name
            std::string shortName = clsName.substr(prefix.size());
            // Skip nested namespaces (only import direct children)
            if (shortName.find('.') == std::string::npos) {
                auto existingIt = importedSymbols_.find(shortName);
                if (existingIt == importedSymbols_.end()) {
                    importedSymbols_[shortName] = clsName;
                }
            }
        }

        // Import methods from classes in this namespace
        for (const auto &m : cls.methods) {
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
            if (existingIt != importedSymbols_.end() && existingIt->second != target) {
                // Conflict - first import wins
                continue;
            }
            importedSymbols_[shortName] = target;
        }

        // Also import properties by their display name (e.g., "Length")
        // mapped to the getter's qualified name (e.g., "Viper.String.get_Length")
        for (const auto &p : cls.properties) {
            if (p.getter && p.name) {
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

    // Import standalone runtime functions and discover sub-namespace prefixes
    // from runtime.def entries not in the RuntimeClasses catalog.
    // Direct children (e.g., Viper.String.Capitalize for bind Viper.String)
    // are imported as short name → qualified name mappings.
    // Sub-namespace prefixes (e.g., Viper.GUI.Shortcuts.Register → "Shortcuts")
    // are imported as module-like symbols.
    for (const auto &alias : il::runtime::kRuntimeNameAliases) {
        std::string canonical(alias.canonical);
        if (canonical.rfind(prefix, 0) != 0)
            continue;
        std::string shortName = canonical.substr(prefix.size());
        auto dotPos = shortName.find('.');
        if (dotPos == std::string::npos) {
            // Direct child — standalone function (e.g., "Capitalize" from bind Viper.String)
            auto existingIt = importedSymbols_.find(shortName);
            if (existingIt == importedSymbols_.end())
                importedSymbols_[shortName] = canonical;
        } else {
            // Sub-namespace prefix
            std::string subNs = shortName.substr(0, dotPos);
            if (importedSymbols_.find(subNs) == importedSymbols_.end())
                importedSymbols_[subNs] = ns + "." + subNs;
        }
    }
}

/// @brief Analyze a global variable declaration, type-checking its initializer.
/// @param decl The global variable declaration to analyze.
void Sema::analyzeGlobalVarDecl(GlobalVarDecl &decl) {
    // Analyze initializer if present
    if (decl.initializer) {
        TypeRef initType = analyzeExpr(decl.initializer.get());

        // If type was inferred, update the symbol
        Symbol *sym = lookupSymbol(decl.name);
        if (sym && sym->type->isUnknown()) {
            sym->type = initType;
        } else if (sym && !sym->type->isAssignableFrom(*initType)) {
            errorTypeMismatch(decl.initializer->loc, sym->type, initType);
        }
    }
}

/// @brief Validate that a type implements all required interface methods.
/// @details For each interface method, checks that the implementing type has a method
///          with matching name and compatible signature, and that it is publicly visible.
/// @param typeName The name of the implementing type.
/// @param interfaces The list of interface names the type claims to implement.
void Sema::validateInterfaceImplementations(const std::string &typeName,
                                            const SourceLoc &loc,
                                            const std::vector<std::string> &interfaces) {
    for (const auto &ifaceName : interfaces) {
        auto ifaceIt = interfaceDecls_.find(ifaceName);
        if (ifaceIt == interfaceDecls_.end()) {
            error(loc, "Unknown interface: " + ifaceName);
            continue;
        }

        bool ok = true;
        InterfaceDecl *iface = ifaceIt->second;
        for (auto &member : iface->members) {
            if (member->kind != DeclKind::Method)
                continue;

            auto *ifaceMethod = static_cast<MethodDecl *>(member.get());
            TypeRef ifaceType = getMethodType(ifaceMethod);
            if (!ifaceType)
                continue;

            MethodDecl *implMethod = nullptr;
            for (auto *candidate : collectMethodOverloads(typeName, ifaceMethod->name, true)) {
                TypeRef candidateType = getMethodType(candidate);
                if (candidateType && candidateType->equals(*ifaceType)) {
                    implMethod = candidate;
                    break;
                }
            }

            if (!implMethod) {
                error(loc,
                      "Type '" + typeName + "' does not implement interface method '" + ifaceName +
                          "." + ifaceMethod->name + "'");
                ok = false;
                continue;
            }

            if (implMethod->visibility != Visibility::Public) {
                error(loc,
                      "Method '" + typeName + "." + implMethod->name +
                          "' must be public to satisfy interface '" + ifaceName + "'");
                ok = false;
            }
        }

        if (ok)
            types::registerInterfaceImplementation(typeName, ifaceName);
    }
}

/// @brief Analyze a value type declaration body (fields, methods, interface validation).
/// @param decl The value type declaration to analyze.
void Sema::analyzeValueDecl(ValueDecl &decl) {
    // Generic types are registered in the first pass; skip body analysis
    if (!decl.genericParams.empty())
        return;

    auto selfType = types::value(decl.name);
    currentSelfType_ = selfType;

    pushScope(decl.loc);

    // Analyze fields
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Field) {
            analyzeFieldDecl(*static_cast<FieldDecl *>(member.get()), selfType);
        }
    }

    // Analyze methods
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            analyzeMethodDecl(*static_cast<MethodDecl *>(member.get()), selfType);
        } else if (member->kind == DeclKind::Property) {
            analyzePropertyDecl(*static_cast<PropertyDecl *>(member.get()), selfType);
        }
    }

    // Validate interface implementations after members are known
    validateInterfaceImplementations(decl.name, decl.loc, decl.interfaces);

    popScope(decl.loc);
    currentSelfType_ = nullptr;
}

/// @brief Register field and method type signatures for a type declaration.
/// @details Populates fieldTypes_ and methodTypes_ maps with qualified keys
///          (typeName.memberName) so cross-type member references resolve correctly.
/// @tparam T The declaration type (EntityDecl, ValueDecl, or InterfaceDecl).
/// @param decl The type declaration whose members should be registered.
/// @param includeFields If true, register field types; false for interface-only registration.
template <typename T> void Sema::registerTypeMembers(T &decl, bool includeFields) {
    // Register field types (if applicable)
    std::unordered_set<std::string> seenFields;
    std::unordered_set<std::string> seenProperties;
    if (includeFields) {
        for (auto &member : decl.members) {
            if (member->kind == DeclKind::Field) {
                auto *field = static_cast<FieldDecl *>(member.get());
                if (!seenFields.insert(field->name).second) {
                    error(field->loc,
                          "Duplicate definition of '" + field->name + "' in type '" + decl.name +
                              "'");
                    continue;
                }
                TypeRef fieldType =
                    field->type ? resolveTypeNode(field->type.get()) : types::unknown();
                std::string fieldKey = decl.name + "." + field->name;
                fieldTypes_[fieldKey] = fieldType;
                memberVisibility_[fieldKey] = field->visibility;
            } else if (member->kind == DeclKind::Property) {
                auto *prop = static_cast<PropertyDecl *>(member.get());
                if (!seenProperties.insert(prop->name).second || seenFields.contains(prop->name)) {
                    error(prop->loc,
                          "Duplicate definition of '" + prop->name + "' in type '" + decl.name +
                              "'");
                    continue;
                }

                TypeRef propType =
                    prop->type ? resolveTypeNode(prop->type.get()) : types::unknown();
                std::string getterKey = decl.name + ".get_" + prop->name;
                TypeRef getterType = types::function({}, propType);
                methodTypes_[getterKey] = getterType;
                memberVisibility_[decl.name + "." + prop->name] = prop->visibility;
                memberVisibility_[getterKey] = prop->visibility;

                if (prop->setterBody) {
                    std::string setterKey = decl.name + ".set_" + prop->name;
                    TypeRef setterType = types::function({propType}, types::voidType());
                    methodTypes_[setterKey] = setterType;
                    memberVisibility_[setterKey] = prop->visibility;
                }
            }
        }
    }

    // Register method types (signatures only, not bodies)
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            TypeRef methodType = methodTypeForDecl(*method);
            if (!registerMethodOverload(decl.name, method, methodType, method->loc))
                continue;

            std::string methodKey = decl.name + "." + method->name;
            if (methodTypes_.find(methodKey) == methodTypes_.end())
                methodTypes_[methodKey] = methodType;
            memberVisibility_[methodKey] = method->visibility;
        }
    }
}

// Explicit template instantiations
template void Sema::registerTypeMembers<EntityDecl>(EntityDecl &, bool);
template void Sema::registerTypeMembers<ValueDecl>(ValueDecl &, bool);
template void Sema::registerTypeMembers<InterfaceDecl>(InterfaceDecl &, bool);

/// @brief Register entity member signatures (fields and methods).
/// @details Skips generic types, which are registered during instantiation.
void Sema::registerEntityMembers(EntityDecl &decl) {
    // Skip member registration for generic types - done during instantiation
    if (!decl.genericParams.empty())
        return;
    registerTypeMembers(decl, true);
}

/// @brief Register value member signatures (fields and methods).
/// @details Skips generic types, which are registered during instantiation.
void Sema::registerValueMembers(ValueDecl &decl) {
    // Skip member registration for generic types - done during instantiation
    if (!decl.genericParams.empty())
        return;
    registerTypeMembers(decl, true);
}

/// @brief Register interface member signatures (methods only, no fields).
void Sema::registerInterfaceMembers(InterfaceDecl &decl) {
    registerTypeMembers(decl, false);
}

/// @brief Analyze an entity type declaration body.
/// @details Handles base class inheritance by copying parent fields and methods into scope,
///          pre-defines method symbols for intra-entity calls, then analyzes member bodies.
///          Validates interface implementations after all members are analyzed.
/// @param decl The entity declaration to analyze.
void Sema::analyzeEntityDecl(EntityDecl &decl) {
    // Generic types are registered in the first pass; skip body analysis
    if (!decl.genericParams.empty())
        return;

    auto selfType = types::entity(decl.name);
    currentSelfType_ = selfType;

    pushScope(decl.loc);

    std::unordered_set<std::string> declaredFieldNames;
    std::vector<MethodDecl *> declaredMethods;
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            declaredFieldNames.insert(field->name);
        } else if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            declaredMethods.push_back(method);
        }
    }

    // BUG-VL-006 fix: Handle inheritance - add parent's members to scope
    if (!decl.baseClass.empty()) {
        auto parentIt = entityDecls_.find(decl.baseClass);
        if (parentIt == entityDecls_.end()) {
            error(decl.loc, "Unknown base class: " + decl.baseClass);
        } else {
            EntityDecl *parent = parentIt->second;
            // BUG-VL-007 fix: Register inheritance for polymorphism support
            types::registerEntityInheritance(decl.name, parent->name);

            // Add ALL parent's fields to this entity's scope (including inherited fields)
            // by scanning fieldTypes_ for entries with parent's name prefix.
            // This ensures grandparent fields are also accessible in grandchildren.
            // NOTE: Collect entries first, then insert — inserting into an
            // unordered_map during iteration can trigger a rehash and
            // invalidate the iterator.
            std::string parentPrefix = parent->name + ".";
            std::vector<std::pair<std::string, TypeRef>> inheritedFields;
            for (const auto &entry : fieldTypes_) {
                if (entry.first.rfind(parentPrefix, 0) == 0) {
                    std::string fieldName = entry.first.substr(parentPrefix.size());
                    if (!declaredFieldNames.contains(fieldName) &&
                        !currentScope_->lookupLocal(fieldName)) {
                        inheritedFields.emplace_back(fieldName, entry.second);
                    }
                }
            }
            for (const auto &[fieldName, fieldType] : inheritedFields) {
                Symbol sym;
                sym.kind = Symbol::Kind::Field;
                sym.name = fieldName;
                sym.type = fieldType;
                defineSymbol(fieldName, sym);
                fieldTypes_[decl.name + "." + fieldName] = fieldType;
            }
        }
    }

    if (!decl.baseClass.empty()) {
        auto parentIt = entityDecls_.find(decl.baseClass);
        if (parentIt != entityDecls_.end()) {
            for (auto *method : declaredMethods) {
                MethodDecl *parentMethod = findInheritedExactMethod(decl.name, *method);
                if (method->isOverride && !parentMethod) {
                    error(method->loc,
                          "Method '" + method->name +
                              "' is marked override but no parent method with the same signature "
                              "exists");
                    continue;
                }

                if (parentMethod && !method->isOverride) {
                    error(method->loc,
                          "Method '" + method->name +
                              "' must be marked override to replace an inherited overload");
                    continue;
                }

                if (parentMethod) {
                    TypeRef parentType = getMethodType(decl.baseClass, parentMethod);
                    TypeRef childType = getMethodType(decl.name, method);
                    if (parentType && childType && !parentType->equals(*childType)) {
                        error(method->loc,
                              "Method '" + method->name +
                                  "' must preserve the inherited return type for an override");
                    }
                }
            }
        }
    }

    // Analyze fields first (adds them to scope)
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Field) {
            analyzeFieldDecl(*static_cast<FieldDecl *>(member.get()), selfType);
        }
    }

    // Pre-define method symbols in scope so they can be called without 'self.'
    // This allows methods to call each other by bare name within the entity.
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            TypeRef methodType = getMethodType(decl.name, method);
            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = method->name;
            sym.type = methodType ? methodType : methodTypeForDecl(*method);
            sym.isFinal = true;
            sym.decl = method;
            if (!currentScope_->lookupLocal(method->name))
                defineSymbol(method->name, sym);
        }
    }

    // Analyze methods (now they can reference each other by bare name)
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            analyzeMethodDecl(*static_cast<MethodDecl *>(member.get()), selfType);
        } else if (member->kind == DeclKind::Property) {
            analyzePropertyDecl(*static_cast<PropertyDecl *>(member.get()), selfType);
        } else if (member->kind == DeclKind::Destructor) {
            analyzeDestructorDecl(*static_cast<DestructorDecl *>(member.get()), selfType);
        }
    }

    // Validate interface implementations
    validateInterfaceImplementations(decl.name, decl.loc, decl.interfaces);

    SourceLoc endLoc = decl.loc;
    if (!decl.members.empty())
        endLoc = decl.members.back()->loc;
    popScope(endLoc);
    currentSelfType_ = nullptr;
}

/// @brief Analyze an interface declaration, registering method signatures.
/// @param decl The interface declaration to analyze.
void Sema::analyzeInterfaceDecl(InterfaceDecl &decl) {
    auto selfType = types::interface(decl.name);
    currentSelfType_ = selfType;

    pushScope(decl.loc);

    // Analyze method signatures
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            TypeRef methodType = getMethodType(decl.name, method);
            if (!methodType)
                methodType = methodTypeForDecl(*method);

            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = method->name;
            sym.type = methodType;
            sym.decl = method;
            if (!currentScope_->lookupLocal(method->name))
                defineSymbol(method->name, sym);
        }
    }

    popScope(decl.loc);
    currentSelfType_ = nullptr;
}

/// @brief Analyze an enum declaration: validate variants and register them.
/// @param decl The enum declaration to analyze.
void Sema::analyzeEnumDecl(EnumDecl &decl) {
    auto enumT = types::enumType(decl.name);
    enumDecls_[decl.name] = &decl;

    // Validate variants and resolve values
    std::unordered_set<std::string> seenNames;
    int64_t nextValue = 0;

    for (auto &variant : decl.variants) {
        // Check for duplicate variant names
        if (!seenNames.insert(variant.name).second) {
            error(variant.loc,
                  "Duplicate enum variant '" + variant.name + "' in '" + decl.name + "'");
            continue;
        }

        // Resolve the value (explicit or auto-increment)
        if (variant.explicitValue.has_value()) {
            nextValue = variant.explicitValue.value();
        } else {
            variant.explicitValue = nextValue;
        }

        // Register the variant as a field-like entry: EnumName.VariantName -> EnumType
        std::string key = decl.name + "." + variant.name;
        fieldTypes_[key] = enumT;

        ++nextValue;
    }
}

/// @brief Analyze a function declaration body (parameters, return type, body statements).
/// @param decl The function declaration to analyze.
void Sema::analyzeFunctionDecl(FunctionDecl &decl) {
    // Generic functions are registered in the first pass; skip body analysis
    // The body will be analyzed when the function is instantiated
    if (!decl.genericParams.empty())
        return;

    currentFunction_ = &decl;
    TypeRef funcType =
        functionDeclTypes_.count(&decl) ? functionDeclTypes_[&decl] : functionTypeForDecl(decl);
    if (decl.isAsync)
        expectedReturnType_ =
            decl.returnType ? resolveTypeNode(decl.returnType.get()) : types::voidType();
    else
        expectedReturnType_ = funcType && funcType->kind == TypeKindSem::Function
                                  ? funcType->returnType()
                                  : types::voidType();

    pushScope(decl.loc);

    // Define parameters
    for (const auto &param : decl.params) {
        TypeRef paramType = param.type ? resolveTypeNode(param.type.get()) : types::unknown();

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true; // Parameters are immutable by default
        defineSymbol(param.name, sym, param.loc);
        markInitialized(param.name);
    }

    // Analyze body
    if (decl.body) {
        analyzeStmt(decl.body.get());

        // W008: Missing return in non-void function
        if (expectedReturnType_ && expectedReturnType_->kind != TypeKindSem::Void) {
            if (!stmtAlwaysExits(decl.body.get())) {
                warn(WarningCode::W008_MissingReturn,
                     decl.loc,
                     "Function '" + decl.name + "' may not return a value on all code paths");
            }
        }
    }

    popScope(decl.body ? scopeEndForStmt(decl.body.get()) : decl.loc);

    currentFunction_ = nullptr;
    expectedReturnType_ = nullptr;
}

/// @brief Analyze a field declaration (type resolution and initializer type checking).
/// @param decl The field declaration to analyze.
/// @param ownerType The type that owns this field.
void Sema::analyzeFieldDecl(FieldDecl &decl, TypeRef ownerType) {
    TypeRef fieldType = decl.type ? resolveTypeNode(decl.type.get()) : types::unknown();

    // Check initializer type
    if (decl.initializer) {
        TypeRef initType = analyzeExpr(decl.initializer.get());
        if (!fieldType->isAssignableFrom(*initType)) {
            errorTypeMismatch(decl.initializer->loc, fieldType, initType);
        }
    }

    // Store field type and visibility for access checking
    if (ownerType) {
        std::string fieldKey = ownerType->name + "." + decl.name;
        if (fieldTypes_.find(fieldKey) == fieldTypes_.end()) {
            fieldTypes_[fieldKey] = fieldType;
            memberVisibility_[fieldKey] = decl.visibility;
        }
    }

    Symbol sym;
    sym.kind = Symbol::Kind::Field;
    sym.name = decl.name;
    sym.type = fieldType;
    sym.isFinal = decl.isFinal;
    sym.decl = &decl;
    defineSymbol(decl.name, sym);
}

void Sema::analyzePropertyDecl(PropertyDecl &decl, TypeRef ownerType) {
    TypeRef propType = decl.type ? resolveTypeNode(decl.type.get()) : types::unknown();

    auto analyzeBody = [&](Stmt *body, TypeRef returnType, bool defineSetterParam) {
        if (!body)
            return;

        TypeRef savedSelfType = currentSelfType_;
        TypeRef savedExpectedReturnType = expectedReturnType_;

        currentSelfType_ = ownerType;
        expectedReturnType_ = returnType;

        pushScope(decl.loc);

        if (!decl.isStatic) {
            Symbol selfSym;
            selfSym.kind = Symbol::Kind::Parameter;
            selfSym.name = "self";
            selfSym.type = ownerType;
            selfSym.isFinal = true;
            defineSymbol("self", selfSym, decl.loc);
            markInitialized("self");
        }

        if (defineSetterParam) {
            Symbol paramSym;
            paramSym.kind = Symbol::Kind::Parameter;
            paramSym.name = decl.setterParam;
            paramSym.type = propType;
            paramSym.isFinal = true;
            defineSymbol(decl.setterParam, paramSym, decl.loc);
            markInitialized(decl.setterParam);
        }

        analyzeStmt(body);

        if (returnType && returnType->kind != TypeKindSem::Void && !stmtAlwaysExits(body)) {
            warn(WarningCode::W008_MissingReturn,
                 decl.loc,
                 "Property '" + decl.name + "' may not return a value on all code paths");
        }

        popScope(scopeEndForStmt(body));
        expectedReturnType_ = savedExpectedReturnType;
        currentSelfType_ = savedSelfType;
    };

    analyzeBody(decl.getterBody.get(), propType, false);
    if (decl.setterBody)
        analyzeBody(decl.setterBody.get(), types::voidType(), true);
}

const PropertyDecl *Sema::findPropertyDecl(const std::string &ownerName,
                                           const std::string &propertyName) const {
    auto scanMembers = [&](const auto *typeDecl) -> const PropertyDecl * {
        if (!typeDecl)
            return nullptr;
        for (const auto &member : typeDecl->members) {
            if (member->kind != DeclKind::Property)
                continue;
            auto *prop = static_cast<PropertyDecl *>(member.get());
            if (prop->name == propertyName)
                return prop;
        }
        return nullptr;
    };

    auto entityIt = entityDecls_.find(ownerName);
    if (entityIt != entityDecls_.end()) {
        if (const PropertyDecl *prop = scanMembers(entityIt->second))
            return prop;
        if (!entityIt->second->baseClass.empty())
            return findPropertyDecl(entityIt->second->baseClass, propertyName);
        return nullptr;
    }

    auto valueIt = valueDecls_.find(ownerName);
    if (valueIt != valueDecls_.end())
        return scanMembers(valueIt->second);

    return nullptr;
}

void Sema::analyzeDestructorDecl(DestructorDecl &decl, TypeRef ownerType) {
    if (!decl.body)
        return;

    TypeRef savedSelfType = currentSelfType_;
    TypeRef savedExpectedReturnType = expectedReturnType_;
    FunctionDecl *savedFunction = currentFunction_;

    currentSelfType_ = ownerType;
    expectedReturnType_ = types::voidType();
    currentFunction_ = nullptr;

    pushScope(decl.loc);

    Symbol selfSym;
    selfSym.kind = Symbol::Kind::Parameter;
    selfSym.name = "self";
    selfSym.type = ownerType;
    selfSym.isFinal = true;
    defineSymbol("self", selfSym, decl.loc);
    markInitialized("self");

    analyzeStmt(decl.body.get());

    popScope(scopeEndForStmt(decl.body.get()));

    currentFunction_ = savedFunction;
    expectedReturnType_ = savedExpectedReturnType;
    currentSelfType_ = savedSelfType;
}

/// @brief Analyze a method declaration body (implicit self parameter, parameters, body).
/// @param decl The method declaration to analyze.
/// @param ownerType The type that owns this method, used as the type of 'self'.
void Sema::analyzeMethodDecl(MethodDecl &decl, TypeRef ownerType) {
    currentSelfType_ = ownerType;
    TypeRef methodType =
        methodDeclTypes_.count(&decl) ? methodDeclTypes_[&decl] : methodTypeForDecl(decl);
    TypeRef returnType = methodType && methodType->kind == TypeKindSem::Function
                             ? methodType->returnType()
                             : types::voidType();
    expectedReturnType_ = returnType;

    // Register method type: "TypeName.methodName" -> function type
    std::string methodKey = ownerType->name + "." + decl.name;
    if (methodTypes_.find(methodKey) == methodTypes_.end()) {
        methodTypes_[methodKey] = methodType;
        memberVisibility_[methodKey] = decl.visibility;
    }

    pushScope(decl.loc);

    // Define 'self' parameter implicitly
    Symbol selfSym;
    selfSym.kind = Symbol::Kind::Parameter;
    selfSym.name = "self";
    selfSym.type = ownerType;
    selfSym.isFinal = true;
    defineSymbol("self", selfSym, decl.loc);
    markInitialized("self");

    // Define explicit parameters
    for (size_t i = 0; i < decl.params.size(); ++i) {
        const auto &param = decl.params[i];
        TypeRef paramType = types::unknown();
        if (methodType && methodType->kind == TypeKindSem::Function) {
            const auto &paramTypes = methodType->paramTypes();
            if (i < paramTypes.size())
                paramType = paramTypes[i];
        } else if (param.type) {
            paramType = resolveTypeNode(param.type.get());
        }

        Symbol sym;
        sym.kind = Symbol::Kind::Parameter;
        sym.name = param.name;
        sym.type = paramType;
        sym.isFinal = true;
        defineSymbol(param.name, sym, param.loc);
        markInitialized(param.name);
    }

    // Analyze body
    if (decl.body) {
        analyzeStmt(decl.body.get());

        // W008: Missing return in non-void method
        if (expectedReturnType_ && expectedReturnType_->kind != TypeKindSem::Void) {
            if (!stmtAlwaysExits(decl.body.get())) {
                warn(WarningCode::W008_MissingReturn,
                     decl.loc,
                     "Method '" + decl.name + "' may not return a value on all code paths");
            }
        }
    }

    popScope(decl.body ? scopeEndForStmt(decl.body.get()) : decl.loc);

    expectedReturnType_ = nullptr;
}


} // namespace il::frontends::zia
