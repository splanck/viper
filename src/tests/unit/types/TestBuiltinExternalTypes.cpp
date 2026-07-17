//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/types/TestBuiltinExternalTypes.cpp
// Purpose: Assert that built-in external namespaced types are present in the registry.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"

#include <cassert>

using namespace il::frontends::basic;

int main() {
    NamespaceRegistry ns;
    seedRuntimeTypeCatalog(ns);

    // Canonical types should be present under their fully-qualified names
    assert(ns.typeExists("Zanna.Core.Object"));
    assert(ns.typeExists("Zanna.String"));
    assert(ns.typeExists("Zanna.Text.StringBuilder"));
    assert(ns.typeExists("Zanna.IO.File"));
    assert(ns.typeExists("Zanna.Collections.List"));

    // Namespaces should exist for imports
    assert(ns.namespaceExists("Zanna"));
    assert(ns.namespaceExists("Zanna.Text"));
    assert(ns.namespaceExists("Zanna.IO"));
    assert(ns.namespaceExists("Zanna.Collections"));
    return 0;
}
