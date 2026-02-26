//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Completion.cpp
/// @brief Completion and tooling query APIs for the Zia semantic analyzer.
///
/// @details Implements the read-only query methods on `Sema` that are used
/// by IDE tooling — in particular the code-completion engine in Phase 2.
/// All methods here are const and only read the symbol tables that were
/// built during analyze(); they never mutate analyzer state.
///
/// ## Implemented APIs
///
/// - `getGlobalSymbols()`   — all symbols in the global (module-level) scope
/// - `getMembersOf(type)`   — fields + methods of a user-defined type, or
///                            delegates to getRuntimeMembers() for Ptr types
/// - `getRuntimeMembers(cls)` — methods + properties from the RuntimeRegistry
/// - `getTypeNames()`       — names of all entity/value/interface declarations
/// - `getBoundModuleNames()` — short aliases from `bind Alias = Namespace;`
/// - `getModuleExports(mod)` — exported symbols of a bound file module
///
/// @see Sema.hpp for declarations and documentation.
/// @see ZiaAnalysis.hpp for parseAndAnalyze(), which creates the Sema object.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/RuntimeAdapter.hpp"
#include "frontends/zia/Sema.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include <unordered_set>

namespace il::frontends::zia
{

// ---------------------------------------------------------------------------
// getGlobalSymbols
// ---------------------------------------------------------------------------

std::vector<Symbol> Sema::getGlobalSymbols() const
{
    std::vector<Symbol> result;
    if (scopes_.empty())
        return result;

    // scopes_[0] is the global (module-level) scope created in Sema::analyze().
    // It contains: top-level funcs, entity/value/interface ctors, bound runtime
    // identifiers, and global variables.  Local variables inside function bodies
    // were popped off the scope stack when their blocks were analyzed.
    for (const auto &[name, sym] : scopes_[0]->getSymbols())
    {
        result.push_back(sym);
    }
    return result;
}

// ---------------------------------------------------------------------------
// getMembersOf
// ---------------------------------------------------------------------------

std::vector<Symbol> Sema::getMembersOf(const TypeRef &type) const
{
    std::vector<Symbol> result;
    if (!type)
        return result;

    // For runtime class pointer types, delegate to getRuntimeMembers().
    if (type->kind == TypeKindSem::Ptr && !type->name.empty())
    {
        return getRuntimeMembers(type->name);
    }

    // For user-defined types, look up in fieldTypes_ and methodTypes_ using
    // "TypeName.memberName" key format (established by Sema_Decl.cpp).
    if (type->kind != TypeKindSem::Entity && type->kind != TypeKindSem::Value &&
        type->kind != TypeKindSem::Interface)
    {
        return result;
    }

    const std::string &typeName = type->name;
    if (typeName.empty())
        return result;

    const std::string prefix = typeName + ".";

    for (const auto &[key, fieldType] : fieldTypes_)
    {
        if (key.rfind(prefix, 0) == 0)
        {
            std::string memberName = key.substr(prefix.size());
            Symbol sym;
            sym.kind = Symbol::Kind::Field;
            sym.name = memberName;
            sym.type = fieldType;
            result.push_back(sym);
        }
    }

    for (const auto &[key, methodType] : methodTypes_)
    {
        if (key.rfind(prefix, 0) == 0)
        {
            std::string memberName = key.substr(prefix.size());
            Symbol sym;
            sym.kind = Symbol::Kind::Method;
            sym.name = memberName;
            sym.type = methodType;
            result.push_back(sym);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// getRuntimeMembers
// ---------------------------------------------------------------------------

std::vector<Symbol> Sema::getRuntimeMembers(const std::string &className) const
{
    std::vector<Symbol> result;
    const auto &catalog = il::runtime::RuntimeRegistry::instance().rawCatalog();

    const il::runtime::RuntimeClass *rtClass = nullptr;
    for (const auto &cls : catalog)
    {
        if (cls.qname && cls.qname == className)
        {
            rtClass = &cls;
            break;
        }
    }

    if (!rtClass)
        return result;

    // Methods — parse the signature to build a function TypeRef.
    for (const auto &method : rtClass->methods)
    {
        if (!method.name)
            continue;

        auto sig = il::runtime::parseRuntimeSignature(method.signature ? method.signature : "");
        TypeRef retType = sig.isValid() ? toZiaReturnType(sig) : types::unknown();

        std::vector<TypeRef> paramTypes;
        if (sig.isValid())
        {
            for (auto ilType : sig.params)
            {
                paramTypes.push_back(toZiaType(ilType));
            }
        }

        Symbol sym;
        sym.kind = Symbol::Kind::Method;
        sym.name = method.name;
        sym.type = types::function(std::move(paramTypes), retType);
        sym.isExtern = true;
        result.push_back(sym);
    }

    // Properties — represent as Field symbols with the property's value type.
    for (const auto &prop : rtClass->properties)
    {
        if (!prop.name)
            continue;

        auto ilType = il::runtime::mapILToken(prop.type ? prop.type : "");
        TypeRef propType = toZiaType(ilType);

        Symbol sym;
        sym.kind = Symbol::Kind::Field;
        sym.name = prop.name;
        sym.type = propType;
        sym.isFinal = prop.readonly;
        sym.isExtern = true;
        result.push_back(sym);
    }

    return result;
}

// ---------------------------------------------------------------------------
// getTypeNames
// ---------------------------------------------------------------------------

std::vector<std::string> Sema::getTypeNames() const
{
    std::vector<std::string> names;
    names.reserve(entityDecls_.size() + valueDecls_.size() + interfaceDecls_.size());

    for (const auto &[name, _] : entityDecls_)
        names.push_back(name);
    for (const auto &[name, _] : valueDecls_)
        names.push_back(name);
    for (const auto &[name, _] : interfaceDecls_)
        names.push_back(name);

    return names;
}

// ---------------------------------------------------------------------------
// getBoundModuleNames
// ---------------------------------------------------------------------------

std::vector<std::string> Sema::getBoundModuleNames() const
{
    std::vector<std::string> names;
    // aliasToNamespace_ maps short alias → full namespace path.
    // The keys are the prefixes users can type before '.'.
    names.reserve(aliasToNamespace_.size());
    for (const auto &[alias, _] : aliasToNamespace_)
    {
        names.push_back(alias);
    }
    return names;
}

// ---------------------------------------------------------------------------
// getModuleExports
// ---------------------------------------------------------------------------

std::vector<Symbol> Sema::getModuleExports(const std::string &moduleName) const
{
    std::vector<Symbol> result;
    auto it = moduleExports_.find(moduleName);
    if (it == moduleExports_.end())
        return result;

    result.reserve(it->second.size());
    for (const auto &[name, sym] : it->second)
    {
        result.push_back(sym);
    }
    return result;
}

// ---------------------------------------------------------------------------
// resolveModuleAlias
// ---------------------------------------------------------------------------

std::string Sema::resolveModuleAlias(const std::string &alias) const
{
    auto it = aliasToNamespace_.find(alias);
    if (it == aliasToNamespace_.end())
        return {};
    return it->second;
}

// ---------------------------------------------------------------------------
// getNamespaceClasses
// ---------------------------------------------------------------------------

std::vector<std::string> Sema::getNamespaceClasses(const std::string &nsPrefix) const
{
    std::vector<std::string> result;
    const std::string nsWithDot = nsPrefix + ".";
    const auto &catalog = il::runtime::RuntimeRegistry::instance().rawCatalog();

    std::unordered_set<std::string> seen;
    for (const auto &cls : catalog)
    {
        if (!cls.qname)
            continue;
        std::string_view qname = cls.qname;
        if (qname.size() <= nsWithDot.size())
            continue;
        if (qname.substr(0, nsWithDot.size()) != nsWithDot)
            continue;

        // Extract the immediate child identifier.
        // e.g.  "Viper.GUI.Canvas"       → "Canvas"
        //        "Viper.GUI.App.Toolbar"  → "App"
        std::string rest(qname.substr(nsWithDot.size()));
        auto dotPos = rest.find('.');
        std::string childName = (dotPos != std::string::npos) ? rest.substr(0, dotPos) : rest;
        if (!childName.empty() && seen.insert(childName).second)
            result.push_back(childName);
    }
    return result;
}

} // namespace il::frontends::zia
