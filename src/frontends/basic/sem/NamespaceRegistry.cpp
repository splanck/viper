//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/NamespaceRegistry.cpp
// Purpose: Implements namespace and type registration with case-insensitive lookups.
// Key invariants:
//   - All internal keys use lowercase for case-insensitive comparison.
//   - First-seen spellings are preserved in NamespaceInfo::full.
//   - Repeated namespace registrations are merged.
// Ownership/Lifetime: Registry is owned by semantic analyzer.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include <algorithm>
#include <cctype>
#include <string_view>

#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::basic
{

std::string NamespaceRegistry::toLower(const std::string &str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
    {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

void NamespaceRegistry::registerNamespace(const std::string &full)
{
    std::string key = toLower(full);
    auto it = namespaces_.find(key);
    if (it == namespaces_.end())
    {
        // First time seeing this namespace; store canonical spelling.
        NamespaceInfo info;
        info.full = full;
        namespaces_[key] = std::move(info);
    }
    // If already exists, preserve first-seen spelling (no-op).
}

void NamespaceRegistry::registerClass(const std::string &nsFull, const std::string &className)
{
    // Ensure namespace exists.
    registerNamespace(nsFull);

    std::string key = toLower(nsFull);
    auto it = namespaces_.find(key);
    // Should always succeed since we just registered it.
    if (it == namespaces_.end())
        return;

    // Build fully-qualified class name using canonical namespace spelling.
    // Handle global namespace (empty string) specially.
    std::string fqClassName;
    if (it->second.full.empty())
        fqClassName = className;
    else
        fqClassName = it->second.full + "." + className;
    it->second.classes.insert(fqClassName);

    // Record type kind for lookups.
    std::string typeKey = toLower(fqClassName);
    types_[typeKey] = TypeKind::Class;
}

void NamespaceRegistry::registerInterface(const std::string &nsFull, const std::string &ifaceName)
{
    // Ensure namespace exists.
    registerNamespace(nsFull);

    std::string key = toLower(nsFull);
    auto it = namespaces_.find(key);
    // Should always succeed since we just registered it.
    if (it == namespaces_.end())
        return;

    // Build fully-qualified interface name using canonical namespace spelling.
    // Handle global namespace (empty string) specially.
    std::string fqIfaceName;
    if (it->second.full.empty())
        fqIfaceName = ifaceName;
    else
        fqIfaceName = it->second.full + "." + ifaceName;
    it->second.interfaces.insert(fqIfaceName);

    // Record type kind for lookups.
    std::string typeKey = toLower(fqIfaceName);
    types_[typeKey] = TypeKind::Interface;
}

bool NamespaceRegistry::namespaceExists(const std::string &full) const
{
    std::string key = toLower(full);
    return namespaces_.find(key) != namespaces_.end();
}

bool NamespaceRegistry::typeExists(const std::string &qualified) const
{
    std::string key = toLower(qualified);
    return types_.find(key) != types_.end();
}

NamespaceRegistry::TypeKind NamespaceRegistry::getTypeKind(const std::string &qualified) const
{
    std::string key = toLower(qualified);
    auto it = types_.find(key);
    if (it == types_.end())
        return TypeKind::None;
    return it->second;
}

const NamespaceRegistry::NamespaceInfo *NamespaceRegistry::info(const std::string &full) const
{
    std::string key = toLower(full);
    auto it = namespaces_.find(key);
    if (it == namespaces_.end())
        return nullptr;
    return &it->second;
}

void NamespaceRegistry::seedFromRuntimeBuiltins(const std::vector<il::runtime::RuntimeDescriptor> &descs)
{
    for (const auto &d : descs)
    {
        std::string_view name = d.name;
        // Only consider dotted names: treat as Namespace.Type or Namespace.Member
        if (name.find('.') == std::string_view::npos)
            continue;

        // Generate all namespace prefixes up to (but not including) the last segment.
        // Example: "Viper.Console.PrintI64" → prefixes: "Viper", "Viper.Console".
        std::string current;
        current.reserve(name.size());
        std::size_t start = 0;
        while (true)
        {
            std::size_t dot = name.find('.', start);
            if (dot == std::string_view::npos)
                break; // stop before final segment (function/type)
            if (!current.empty())
                current.push_back('.');
            current.append(name.substr(start, dot - start));
            // Register this namespace prefix (idempotent; preserves first-seen casing).
            registerNamespace(current);
            start = dot + 1;
        }
    }
}

} // namespace il::frontends::basic
