//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/TypeResolver.cpp
// Purpose: Implements compile-time type resolution with namespace/using context.
// Key invariants:
//   - Qualified names bypass USING imports.
//   - Simple names use precedence: current NS chain → USING imports.
//   - Ambiguity produces sorted contender lists for stable diagnostics.
// Ownership/Lifetime: TypeResolver does not own registry or context.
// Links: docs/codemap.md, CLAUDE.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/TypeResolver.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace il::frontends::basic
{

TypeResolver::TypeResolver(const NamespaceRegistry &ns, const UsingContext &uc)
    : registry_(ns), using_(uc)
{
}

std::string TypeResolver::toLower(const std::string &str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
    {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

std::string TypeResolver::joinPath(const std::vector<std::string> &segments)
{
    if (segments.empty())
        return "";

    std::string result = segments[0];
    for (size_t i = 1; i < segments.size(); ++i)
    {
        result += ".";
        result += segments[i];
    }
    return result;
}

std::vector<std::string> TypeResolver::splitPath(const std::string &path)
{
    std::vector<std::string> segments;
    std::string current;

    for (char c : path)
    {
        if (c == '.')
        {
            if (!current.empty())
            {
                segments.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(c);
        }
    }

    if (!current.empty())
        segments.push_back(current);

    return segments;
}

std::string TypeResolver::tryResolveInNamespace(const std::string &ns,
                                                const std::string &typeName) const
{
    std::string candidate = ns.empty() ? typeName : (ns + "." + typeName);

    if (registry_.typeExists(candidate))
    {
        // Return the canonical spelling from registry.
        auto kind = registry_.getTypeKind(candidate);
        // We need to get the canonical name; for now return the candidate
        // since registry doesn't expose a "get canonical name" method.
        // The registry stores canonical spellings internally.
        return candidate;
    }

    return "";
}

TypeResolver::Kind TypeResolver::convertKind(NamespaceRegistry::TypeKind nsk)
{
    switch (nsk)
    {
        case NamespaceRegistry::TypeKind::Class:
            return Kind::Class;
        case NamespaceRegistry::TypeKind::Interface:
            return Kind::Interface;
        case NamespaceRegistry::TypeKind::None:
            return Kind::Unknown;
    }
    return Kind::Unknown;
}

TypeResolver::Result TypeResolver::resolve(std::string name,
                                           const std::vector<std::string> &currentNsChain) const
{
    Result result;

    // Check if name contains '.'.
    bool isQualified = name.find('.') != std::string::npos;

    if (isQualified)
    {
        // Qualified name handling.
        auto segments = splitPath(name);
        if (segments.empty())
        {
            // Malformed name.
            return result;
        }

        std::string firstSegment = segments[0];

        // Check if first segment is an alias.
        if (using_.hasAlias(firstSegment))
        {
            // Expand alias.
            std::string aliasedNs = using_.resolveAlias(firstSegment);

            // Build expanded path: aliasedNs + tail segments.
            std::vector<std::string> expandedSegments;
            auto aliasSegs = splitPath(aliasedNs);
            expandedSegments.insert(expandedSegments.end(), aliasSegs.begin(), aliasSegs.end());
            expandedSegments.insert(expandedSegments.end(), segments.begin() + 1, segments.end());

            std::string expandedPath = joinPath(expandedSegments);

            if (registry_.typeExists(expandedPath))
            {
                result.found = true;
                result.qname = expandedPath;
                result.kind = convertKind(registry_.getTypeKind(expandedPath));
                return result;
            }

            // Not found after alias expansion.
            return result;
        }

        // Treat as fully-qualified name.
        if (registry_.typeExists(name))
        {
            result.found = true;
            result.qname = name;
            result.kind = convertKind(registry_.getTypeKind(name));
            return result;
        }

        // Not found.
        return result;
    }

    // Simple name: walk up namespace chain.
    std::vector<std::string> candidates;

    // Try current namespace chain walk-up: A.B.C.T → A.B.T → A.T → T.
    for (int depth = static_cast<int>(currentNsChain.size()); depth >= 0; --depth)
    {
        std::vector<std::string> nsSegments(currentNsChain.begin(), currentNsChain.begin() + depth);
        std::string ns = joinPath(nsSegments);

        std::string resolved = tryResolveInNamespace(ns, name);
        if (!resolved.empty())
        {
            result.found = true;
            result.qname = resolved;
            result.kind = convertKind(registry_.getTypeKind(resolved));
            return result;
        }
    }

    // Try USING imports in declaration order.
    for (const auto &import : using_.imports())
    {
        std::string resolved = tryResolveInNamespace(import.ns, name);
        if (!resolved.empty())
        {
            candidates.push_back(resolved);
        }
    }

    // Check candidate count.
    if (candidates.empty())
    {
        // Not found.
        return result;
    }

    if (candidates.size() == 1)
    {
        // Found unique match.
        result.found = true;
        result.qname = candidates[0];
        result.kind = convertKind(registry_.getTypeKind(candidates[0]));
        return result;
    }

    // Ambiguous: sort candidates case-insensitively for stable diagnostics.
    std::sort(candidates.begin(),
              candidates.end(),
              [](const std::string &a, const std::string &b) { return toLower(a) < toLower(b); });

    result.found = false;
    result.contenders = std::move(candidates);
    return result;
}

} // namespace il::frontends::basic
