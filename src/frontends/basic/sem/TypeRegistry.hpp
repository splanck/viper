//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/TypeRegistry.hpp
// Purpose: Seed read-only entries for built-in namespaced runtime types.
// Key invariants:
//   - Entries are catalog-only (no methods/fields exposed yet).
//   - Qualified names live under the reserved root 'Viper'.
//   - Seeding registers namespaces and class/interface names in NamespaceRegistry.
// Ownership/Lifetime: Seeding writes into a caller-owned NamespaceRegistry.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{
struct RuntimeClass; // fwd decl
}

namespace il::frontends::basic
{

class NamespaceRegistry;

/// @brief Category for built-in external types to seed via legacy catalog.
enum class ExternalTypeCategory
{
    Class,
    Interface,
};

/// @brief Catalog entry describing a built-in external type (legacy seed).
struct BuiltinExternalType
{
    const char *qualifiedName;
    ExternalTypeCategory category;
    const char *tag;
};

/// @brief Type classification for TypeRegistry entries.
enum class TypeKind
{
    Unknown,
    BuiltinExternalType,  // Legacy name kept for compatibility with existing tests
    BuiltinExternalClass, // Preferred name for runtime class entries
};

/// @brief Registry of known type names discovered from the runtime class catalog.
/// @details Provides case-insensitive lookup. BASIC alias "STRING" resolves to
///          the same entry as "Viper.String".
class TypeRegistry
{
  public:
    /// @brief Register all runtime classes as BuiltinExternalType entries.
    void seedRuntimeClasses(const std::vector<il::runtime::RuntimeClass> &classes);

    /// @brief Lookup kind for a qualified type name (case-insensitive).
    [[nodiscard]] TypeKind kindOf(std::string_view qualifiedName) const;

  private:
    static std::string toLower(std::string_view s);
    std::unordered_map<std::string, TypeKind> kinds_;
};

/// @brief Access the process-wide TypeRegistry singleton.
TypeRegistry &runtimeTypeRegistry();

/// @brief Seed known built-in external types into the namespace registry (legacy seed).
void seedRuntimeTypeCatalog(NamespaceRegistry &registry);

} // namespace il::frontends::basic
