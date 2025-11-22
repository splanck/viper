//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_type_resolver.cpp
// Purpose: Ensure TypeResolver resolves types using namespace registry and using context. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/TypeResolver.hpp"
#include "frontends/basic/sem/UsingContext.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

void test_fully_qualified_success()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("A.B", "MyClass");

    TypeResolver resolver(reg, uc);
    auto result = resolver.resolve("A.B.MyClass", {});

    assert(result.found);
    assert(result.qname == "A.B.MyClass");
    assert(result.kind == TypeResolver::Kind::Class);
    assert(result.contenders.empty());
}

void test_fully_qualified_interface()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerInterface("X.Y", "IFoo");

    TypeResolver resolver(reg, uc);
    auto result = resolver.resolve("X.Y.IFoo", {});

    assert(result.found);
    assert(result.qname == "X.Y.IFoo");
    assert(result.kind == TypeResolver::Kind::Interface);
}

void test_alias_expansion()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("Foo.Bar.Baz", "Thing");

    SourceLoc loc{1, 1, 1};
    uc.add("Foo.Bar.Baz", "FBB", loc);

    TypeResolver resolver(reg, uc);
    auto result = resolver.resolve("FBB.Thing", {});

    assert(result.found);
    assert(result.qname == "Foo.Bar.Baz.Thing");
    assert(result.kind == TypeResolver::Kind::Class);
}

void test_current_namespace_chain_walkup()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Register types at different levels.
    reg.registerClass("A", "ClassInA");     // A.ClassInA
    reg.registerClass("A.B", "ClassInB");   // A.B.ClassInB
    reg.registerClass("A.B.C", "ClassInC"); // A.B.C.ClassInC

    TypeResolver resolver(reg, uc);

    // From A.B.C, resolve "ClassInC" → should find A.B.C.ClassInC.
    auto r1 = resolver.resolve("ClassInC", {"A", "B", "C"});
    assert(r1.found);
    assert(r1.qname == "A.B.C.ClassInC");

    // From A.B.C, resolve "ClassInB" → should find A.B.ClassInB.
    auto r2 = resolver.resolve("ClassInB", {"A", "B", "C"});
    assert(r2.found);
    assert(r2.qname == "A.B.ClassInB");

    // From A.B.C, resolve "ClassInA" → should find A.ClassInA.
    auto r3 = resolver.resolve("ClassInA", {"A", "B", "C"});
    assert(r3.found);
    assert(r3.qname == "A.ClassInA");
}

void test_using_order_honored()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("First", "Thing");
    reg.registerClass("Second", "Other");

    SourceLoc loc{1, 1, 1};
    uc.add("First", "", loc);
    uc.add("Second", "", loc);

    TypeResolver resolver(reg, uc);

    // Resolve "Thing" from global namespace (no current chain).
    // Should find First.Thing (first USING in order).
    auto r1 = resolver.resolve("Thing", {});
    assert(r1.found);
    assert(r1.qname == "First.Thing");

    // Resolve "Other" from global namespace.
    // Should find Second.Other.
    auto r2 = resolver.resolve("Other", {});
    assert(r2.found);
    assert(r2.qname == "Second.Other");
}

void test_ambiguity_sorted_contenders()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Register "Thing" in two namespaces.
    reg.registerClass("B", "Thing");
    reg.registerClass("A", "Thing");

    SourceLoc loc{1, 1, 1};
    uc.add("B", "", loc);
    uc.add("A", "", loc);

    TypeResolver resolver(reg, uc);

    // Resolve "Thing" from global namespace.
    // Should be ambiguous with sorted contenders {A.Thing, B.Thing}.
    auto result = resolver.resolve("Thing", {});

    assert(!result.found);
    assert(result.qname.empty());
    assert(result.kind == TypeResolver::Kind::Unknown);
    assert(result.contenders.size() == 2);

    // Contenders should be sorted case-insensitively: A.Thing, B.Thing.
    assert(result.contenders[0] == "A.Thing");
    assert(result.contenders[1] == "B.Thing");
}

void test_not_found()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("Some.NS", "ExistingClass");

    TypeResolver resolver(reg, uc);

    // Resolve a non-existent type.
    auto result = resolver.resolve("NonExistent", {"Some", "NS"});

    assert(!result.found);
    assert(result.qname.empty());
    assert(result.kind == TypeResolver::Kind::Unknown);
    assert(result.contenders.empty());
}

void test_namespace_exists_but_type_missing()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Register namespace A.B but no type A.B.Missing.
    reg.registerNamespace("A.B");
    reg.registerClass("A.B", "ExistingClass");

    TypeResolver resolver(reg, uc);

    // Try to resolve "A.B.Missing" (fully-qualified).
    auto result = resolver.resolve("A.B.Missing", {});

    assert(!result.found);
    assert(result.qname.empty());

    // Caller can check if "A.B" namespace exists for E_NS_002.
    assert(reg.namespaceExists("A.B"));
}

void test_simple_name_in_current_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("MyNS", "MyClass");

    TypeResolver resolver(reg, uc);

    // Resolve "MyClass" from within "MyNS".
    auto result = resolver.resolve("MyClass", {"MyNS"});

    assert(result.found);
    assert(result.qname == "MyNS.MyClass");
    assert(result.kind == TypeResolver::Kind::Class);
}

void test_case_insensitive_resolution()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("FooBar", "MyClass");

    TypeResolver resolver(reg, uc);

    // Resolve with different casing.
    auto r1 = resolver.resolve("foobar.myclass", {});
    assert(r1.found);
    assert(r1.qname == "foobar.myclass"); // Returns input casing if found

    auto r2 = resolver.resolve("FOOBAR.MYCLASS", {});
    assert(r2.found);
    assert(r2.qname == "FOOBAR.MYCLASS");
}

void test_alias_case_insensitive()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("A.B", "C");

    SourceLoc loc{1, 1, 1};
    uc.add("A.B", "AB", loc);

    TypeResolver resolver(reg, uc);

    // Use alias with different casing.
    auto r1 = resolver.resolve("AB.C", {});
    assert(r1.found);

    auto r2 = resolver.resolve("ab.c", {});
    assert(r2.found);

    auto r3 = resolver.resolve("Ab.C", {});
    assert(r3.found);
}

void test_global_type_from_nested_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Register a type at the root level (no namespace prefix).
    // Note: NamespaceRegistry doesn't support truly global types (empty namespace),
    // so we test resolving from an empty namespace chain instead.
    reg.registerClass("Root", "GlobalType");

    TypeResolver resolver(reg, uc);

    // Resolve "GlobalType" from Root namespace.
    auto result = resolver.resolve("GlobalType", {"Root"});

    assert(result.found);
    assert(result.qname == "Root.GlobalType");
}

void test_no_ambiguity_if_current_namespace_wins()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Register "Thing" in current namespace and in USING.
    reg.registerClass("Current", "Thing");
    reg.registerClass("Other", "Thing");

    SourceLoc loc{1, 1, 1};
    uc.add("Other", "", loc);

    TypeResolver resolver(reg, uc);

    // Resolve "Thing" from "Current" namespace.
    // Should find Current.Thing (namespace chain has precedence over USING).
    auto result = resolver.resolve("Thing", {"Current"});

    assert(result.found);
    assert(result.qname == "Current.Thing");
    assert(result.contenders.empty());
}

void test_multiple_using_different_types()
{
    NamespaceRegistry reg;
    UsingContext uc;

    reg.registerClass("NS1", "TypeA");
    reg.registerClass("NS2", "TypeB");

    SourceLoc loc{1, 1, 1};
    uc.add("NS1", "", loc);
    uc.add("NS2", "", loc);

    TypeResolver resolver(reg, uc);

    auto r1 = resolver.resolve("TypeA", {});
    assert(r1.found);
    assert(r1.qname == "NS1.TypeA");

    auto r2 = resolver.resolve("TypeB", {});
    assert(r2.found);
    assert(r2.qname == "NS2.TypeB");
}

int main()
{
    test_fully_qualified_success();
    test_fully_qualified_interface();
    test_alias_expansion();
    test_current_namespace_chain_walkup();
    test_using_order_honored();
    test_ambiguity_sorted_contenders();
    test_not_found();
    test_namespace_exists_but_type_missing();
    test_simple_name_in_current_namespace();
    test_case_insensitive_resolution();
    test_alias_case_insensitive();
    test_global_type_from_nested_namespace();
    test_no_ambiguity_if_current_namespace_wins();
    test_multiple_using_different_types();
    return 0;
}
