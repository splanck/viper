//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_class_seeding.cpp
// Purpose: Validate seeding of TypeRegistry, Property/Method indexes, and NamespaceRegistry from
// class catalog. Key invariants: To be documented. Ownership/Lifetime: To be documented. Links:
// docs/architecture.md
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

int main()
{
    // Build an empty program and run the registry builder to trigger seeding.
    Program prog;
    NamespaceRegistry ns;
    UsingContext us;
    buildNamespaceRegistry(prog, ns, us, nullptr);

    // Seed class-driven registries (types, props, methods, namespaces)
    seedRuntimeClassCatalogs(ns);

    // 1) TypeRegistry: Viper.String recognized as BuiltinExternalType
    auto &tyreg = runtimeTypeRegistry();
    assert(tyreg.kindOf("Viper.String") == TypeKind::BuiltinExternalType);
    // STRING alias behaves like Viper.String
    assert(tyreg.kindOf("STRING") == TypeKind::BuiltinExternalType);
    // And Viper.System.String recognized as BuiltinExternalClass
    assert(tyreg.kindOf("Viper.System.String") == TypeKind::BuiltinExternalClass);

    // 2) PropertyIndex: Viper.String.Length exists
    auto &pidx = runtimePropertyIndex();
    auto p = pidx.find("Viper.String", "Length");
    assert(p.has_value());
    assert(p->readonly);
    assert(p->type == "i64");
    // Viper.System.String.Length exists (maps to Viper.Strings.Len)
    auto psys = pidx.find("Viper.System.String", "Length");
    assert(psys.has_value());

    // 3) NamespaceRegistry: Viper.String exists as a namespace prefix
    assert(ns.namespaceExists("Viper"));
    assert(ns.namespaceExists("Viper.String"));

    // 4) MethodIndex sanity: Substring arity 2 exists
    auto &midx = runtimeMethodIndex();
    auto m = midx.find("Viper.String", "Substring", 2);
    assert(m.has_value());
    assert(m->ret == BasicType::String);
    assert(m->args.size() == 2);
    auto msys = midx.find("Viper.System.String", "Substring", 2);
    assert(msys.has_value());
    assert(msys->ret == BasicType::String);
    assert(msys->args.size() == 2);

    // 5) Additional System types appear in TypeRegistry as builtin externals
    assert(tyreg.kindOf("Viper.System.Object") == TypeKind::BuiltinExternalType);
    assert(tyreg.kindOf("Viper.System.IO.File") == TypeKind::BuiltinExternalType);
    assert(tyreg.kindOf("Viper.System.Collections.List") == TypeKind::BuiltinExternalType);

    return 0;
}
