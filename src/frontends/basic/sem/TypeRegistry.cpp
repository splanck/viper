//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/TypeRegistry.cpp
// Purpose: Implements seeding of catalog-only built-in namespaced runtime types.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/TypeRegistry.hpp"
#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "il/runtime/RuntimeClassNames.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

namespace
{

static void ensureNamespaceChain(NamespaceRegistry &registry, const std::string &qualifiedNs)
{
    if (qualifiedNs.empty())
        return;
    // Register each prefix namespace: Viper -> Viper.System -> Viper.System.Text
    std::vector<std::string> segs;
    std::string cur;
    for (char c : qualifiedNs)
    {
        if (c == '.')
        {
            segs.push_back(cur);
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        segs.push_back(cur);

    std::string accum;
    for (std::size_t i = 0; i < segs.size(); ++i)
    {
        if (i)
            accum.push_back('.');
        accum += segs[i];
        registry.registerNamespace(accum);
    }
}

} // namespace

std::string TypeRegistry::toLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

void TypeRegistry::seedRuntimeClasses(const std::vector<il::runtime::RuntimeClass> &classes)
{
    for (const auto &cls : classes)
    {
        std::string key = toLower(cls.qname);
        kinds_[key] = TypeKind::BuiltinExternalType; // default classification for compatibility
    }
    // Prefer the newer BuiltinExternalClass tag for Viper.System.String specifically.
    // Keep Viper.String under BuiltinExternalType for backward compatibility.
    auto itSysStr = kinds_.find("viper.system.string");
    if (itSysStr != kinds_.end())
        itSysStr->second = TypeKind::BuiltinExternalClass;

    // Add BASIC alias: STRING → Viper.String (compat choice). Both names refer to
    // the same nominal runtime class surface in practice.
    kinds_[toLower("Viper.String")] = TypeKind::BuiltinExternalType;
}

TypeKind TypeRegistry::kindOf(std::string_view qualifiedName) const
{
    std::string key = toLower(qualifiedName);
    if (key == "string")
    {
        // BASIC alias resolves to Viper.String for compatibility; callers that
        // want the System-qualified name can ask for "Viper.System.String".
        auto it = kinds_.find("viper.string");
        if (it != kinds_.end())
            return it->second;
        // Fallback: try Viper.System.String when present
        auto it2 = kinds_.find("viper.system.string");
        if (it2 != kinds_.end())
            return it2->second;
    }
    auto it = kinds_.find(key);
    if (it == kinds_.end())
        return TypeKind::Unknown;
    return it->second;
}

TypeRegistry &runtimeTypeRegistry()
{
    static TypeRegistry reg;
    return reg;
}

void seedRuntimeTypeCatalog(NamespaceRegistry &registry)
{
    // Seed a minimal catalog of built-in runtime types. Canonical names live
    // under Viper.* and are defined by the runtime class catalog
    // (src/il/runtime/classes/RuntimeClasses.inc).
    static const BuiltinExternalType kTypes[] = {
        // Canonical forms (from RuntimeClassNames.hpp constants)
        {il::runtime::RTCLASS_OBJECT.data(), ExternalTypeCategory::Class, "viper:Object"},
        {il::runtime::RTCLASS_STRING.data(), ExternalTypeCategory::Class, "viper:String"},
        {il::runtime::RTCLASS_STRINGBUILDER.data(),
         ExternalTypeCategory::Class,
         "viper.text:StringBuilder"},
        {il::runtime::RTCLASS_FILE.data(), ExternalTypeCategory::Class, "viper.io:File"},
        {il::runtime::RTCLASS_LIST.data(), ExternalTypeCategory::Class, "viper.coll:List"},
    };

    for (const auto &entry : kTypes)
    {
        // Split qualified name into namespace and leaf type name.
        std::string qn(entry.qualifiedName);
        std::size_t lastDot = qn.rfind('.');
        if (lastDot == std::string::npos)
            continue;
        std::string ns = qn.substr(0, lastDot);
        std::string leaf = qn.substr(lastDot + 1);

        // Ensure namespace chain exists.
        ensureNamespaceChain(registry, ns);

        // Register the type into its namespace.
        switch (entry.category)
        {
            case ExternalTypeCategory::Class:
                registry.registerClass(ns, leaf);
                break;
            case ExternalTypeCategory::Interface:
                registry.registerInterface(ns, leaf);
                break;
        }
    }

    // Also register namespaces for builtin extern procedure groups so USING works:
    // e.g., USING Viper.Console -> enables unqualified PrintI64 resolution.
    ensureNamespaceChain(registry, std::string("Viper"));
    ensureNamespaceChain(registry, std::string("Viper.Console"));
}

} // namespace il::frontends::basic
