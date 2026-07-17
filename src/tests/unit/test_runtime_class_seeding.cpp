//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_class_seeding.cpp
// Purpose: Validate seeding of TypeRegistry, Property/Method indexes, and NamespaceRegistry from
// class catalog. Key invariants: To be documented. Ownership/Lifetime: To be documented. Links:
// docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AST.hpp"
#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/RegistryBuilder.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "frontends/basic/sem/RuntimePropertyIndex.hpp"
#include "frontends/basic/sem/TypeRegistry.hpp"
#include "frontends/basic/sem/UsingContext.hpp"

#include <cassert>

using namespace il::frontends::basic;

int main() {
    // Build an empty program and run the registry builder to trigger seeding.
    Program prog;
    NamespaceRegistry ns;
    UsingContext us;
    buildNamespaceRegistry(prog, ns, us, nullptr);

    // Seed class-driven registries (types, props, methods, namespaces)
    seedRuntimeClassCatalogs(ns);

    // 1) TypeRegistry: Zanna.String recognized as BuiltinExternalType
    auto &tyreg = runtimeTypeRegistry();
    assert(tyreg.kindOf("Zanna.String") == TypeKind::BuiltinExternalType);
    // STRING alias behaves like Zanna.String
    assert(tyreg.kindOf("STRING") == TypeKind::BuiltinExternalType);

    // 2) PropertyIndex: Zanna.String.get_Length exists
    auto &pidx = runtimePropertyIndex();
    auto p = pidx.find("Zanna.String", "Length");
    assert(p.has_value());
    assert(p->readonly);
    assert(p->type == "i64");

    // 3) NamespaceRegistry: Zanna.String exists as a namespace prefix
    assert(ns.namespaceExists("Zanna"));
    assert(ns.namespaceExists("Zanna.String"));

    // 4) MethodIndex sanity: Substring arity 2 exists
    auto &midx = runtimeMethodIndex();
    auto m = midx.find("Zanna.String", "Substring", 2);
    assert(m.has_value());
    assert(m->ret == BasicType::String);
    assert(m->args.size() == 2);

    // 5) Canonical Zanna.* types appear in TypeRegistry as builtin externals
    assert(tyreg.kindOf("Zanna.Core.Object") == TypeKind::BuiltinExternalType);
    assert(tyreg.kindOf("Zanna.IO.File") == TypeKind::BuiltinExternalType);
    assert(tyreg.kindOf("Zanna.Collections.List") == TypeKind::BuiltinExternalType);

    return 0;
}
