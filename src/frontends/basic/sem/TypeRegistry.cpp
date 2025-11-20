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

void seedRuntimeTypeCatalog(NamespaceRegistry &registry)
{
    // Minimal catalog of canonical types under Viper.System.*. Methods/fields are TBD.
    static const BuiltinExternalType kTypes[] = {
        {"Viper.System.Object", ExternalTypeCategory::Class, "sys:Object"},
        {"Viper.System.String", ExternalTypeCategory::Class, "sys:String"},
        {"Viper.System.Text.StringBuilder", ExternalTypeCategory::Class, "sys.text:StringBuilder"},
        {"Viper.System.IO.File", ExternalTypeCategory::Class, "sys.io:File"},
        {"Viper.System.Collections.List", ExternalTypeCategory::Class, "sys.coll:List"},
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
