//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/types/TestBuiltinExternalTypes.cpp
// Purpose: Assert that built-in external namespaced types are present in the registry.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"

#include <cassert>

using namespace il::frontends::basic;

int main()
{
    NamespaceRegistry ns;
    seedRuntimeTypeCatalog(ns);

    // Canonical types should be present under their fully-qualified names
    assert(ns.typeExists("Viper.Object"));
    assert(ns.typeExists("Viper.String"));
    assert(ns.typeExists("Viper.Text.StringBuilder"));
    assert(ns.typeExists("Viper.IO.File"));
    assert(ns.typeExists("Viper.Collections.List"));

    // Compat aliases (Viper.System.*) should also exist for backward compatibility
    assert(ns.typeExists("Viper.System.Object"));
    assert(ns.typeExists("Viper.System.String"));
    assert(ns.typeExists("Viper.System.Text.StringBuilder"));
    assert(ns.typeExists("Viper.System.IO.File"));
    assert(ns.typeExists("Viper.System.Collections.List"));

    // Namespaces should exist for imports
    assert(ns.namespaceExists("Viper"));
    assert(ns.namespaceExists("Viper.Text"));
    assert(ns.namespaceExists("Viper.IO"));
    assert(ns.namespaceExists("Viper.Collections"));
    // Compat namespaces
    assert(ns.namespaceExists("Viper.System"));
    assert(ns.namespaceExists("Viper.System.Text"));
    return 0;
}
