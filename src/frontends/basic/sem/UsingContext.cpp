//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/UsingContext.cpp
// Purpose: Implements file-scoped USING directive tracking with alias resolution.
// Key invariants:
//   - imports_ vector maintains source declaration order.
//   - alias_ map uses lowercase keys for case-insensitive lookups.
//   - Empty alias string in Import indicates no AS clause.
// Ownership/Lifetime: Owned by per-file semantic context.
// Links: docs/internals/codemap.md, CLAUDE.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/UsingContext.hpp"
#include <algorithm>
#include <cctype>

namespace il::frontends::basic {

/// @brief Lowercase a string to form the case-insensitive alias key.
std::string UsingContext::toLower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

/// @brief Record a `USING` directive.
/// @param ns The imported namespace.
/// @param alias Optional alias (empty when there is no `AS` clause).
/// @param loc Source location of the directive.
/// @details Appends to the declaration-order import list and, when an alias is given, registers
///          it for case-insensitive lookup.
void UsingContext::add(std::string_view ns, std::string_view alias, il::support::SourceLoc loc) {
    // Append to declaration-order vector.
    Import import;
    import.ns = std::string(ns);
    import.alias = std::string(alias);
    import.loc = loc;
    imports_.push_back(std::move(import));

    // Register alias for case-insensitive lookup if present.
    if (!alias.empty()) {
        std::string key = toLower(alias);
        alias_[key] = imports_.back().ns;
    }
}

/// @brief Test whether an alias was registered (case-insensitive).
bool UsingContext::hasAlias(std::string_view alias) const {
    std::string key = toLower(alias);
    return alias_.find(key) != alias_.end();
}

/// @brief Resolve an alias to its namespace.
/// @return The namespace, or "" if the alias is unknown.
std::string UsingContext::resolveAlias(std::string_view alias) const {
    std::string key = toLower(alias);
    auto it = alias_.find(key);
    if (it == alias_.end())
        return "";
    return it->second;
}

/// @brief Reset all imports and aliases (e.g. when starting a new file).
void UsingContext::clear() {
    imports_.clear();
    alias_.clear();
}

} // namespace il::frontends::basic
