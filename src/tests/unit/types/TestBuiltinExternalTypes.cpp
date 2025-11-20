// File: tests/unit/types/TestBuiltinExternalTypes.cpp
// Purpose: Assert that built-in external namespaced types are present in the registry.

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"

#include <cassert>

using namespace il::frontends::basic;

int main()
{
    NamespaceRegistry ns;
    seedRuntimeTypeCatalog(ns);

    // Type should be present under its fully-qualified name
    assert(ns.typeExists("Viper.System.Text.StringBuilder"));
    // Namespaces should also exist for imports
    assert(ns.namespaceExists("Viper"));
    assert(ns.namespaceExists("Viper.System"));
    assert(ns.namespaceExists("Viper.System.Text"));
    return 0;
}
