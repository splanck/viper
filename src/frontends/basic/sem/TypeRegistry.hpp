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

namespace il::frontends::basic
{

class NamespaceRegistry;

/// @brief Category for built-in external types to seed.
enum class ExternalTypeCategory
{
    Class,
    Interface,
};

/// @brief Catalog entry describing a built-in external type.
struct BuiltinExternalType
{
    /// Fully-qualified canonical name (e.g., "Viper.System.Text.StringBuilder").
    const char *qualifiedName;

    /// Category (class vs interface) as exposed to BASIC.
    ExternalTypeCategory category;

    /// Opaque tag reserved for future expansion (ABI, runtime id, etc.).
    const char *tag;
};

/// @brief Seed known built-in external types into the namespace registry.
/// @details Registers the containing namespaces and the class/interface names so
///          the type resolver can recognize them in declarations (e.g., DIM ... AS ...).
///          Methods and fields are intentionally omitted in this phase.
void seedRuntimeTypeCatalog(NamespaceRegistry &registry);

} // namespace il::frontends::basic
