// File: tests/unit/test_namespace_registry.cpp
// Purpose: Ensure NamespaceRegistry handles case-insensitive registration and lookups.

#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;

void test_register_namespace_merges_repeated_blocks()
{
    NamespaceRegistry reg;

    // Register the same namespace multiple times with different casings.
    reg.registerNamespace("A.B");
    reg.registerNamespace("a.b");
    reg.registerNamespace("A.b");

    // All should resolve to the same namespace.
    assert(reg.namespaceExists("A.B"));
    assert(reg.namespaceExists("a.b"));
    assert(reg.namespaceExists("A.b"));

    // Canonical spelling should be the first-seen ("A.B").
    const auto *info = reg.info("a.b");
    assert(info != nullptr);
    assert(info->full == "A.B");
}

void test_register_class_creates_fq_name()
{
    NamespaceRegistry reg;

    reg.registerNamespace("Foo.Bar");
    reg.registerClass("Foo.Bar", "MyClass");

    // Type should exist with fully-qualified name.
    assert(reg.typeExists("Foo.Bar.MyClass"));
    assert(reg.typeExists("foo.bar.myclass"));
    assert(reg.typeExists("FOO.BAR.MYCLASS"));

    // Type kind should be Class.
    assert(reg.getTypeKind("Foo.Bar.MyClass") == NamespaceRegistry::TypeKind::Class);
    assert(reg.getTypeKind("foo.bar.myclass") == NamespaceRegistry::TypeKind::Class);

    // Namespace should contain the class.
    const auto *info = reg.info("foo.bar");
    assert(info != nullptr);
    assert(info->classes.size() == 1);
    assert(info->classes.count("Foo.Bar.MyClass") == 1);
}

void test_register_interface_creates_fq_name()
{
    NamespaceRegistry reg;

    reg.registerNamespace("A.B");
    reg.registerInterface("A.B", "IFoo");

    // Type should exist with fully-qualified name.
    assert(reg.typeExists("A.B.IFoo"));
    assert(reg.typeExists("a.b.ifoo"));
    assert(reg.typeExists("A.b.IFoo"));

    // Type kind should be Interface.
    assert(reg.getTypeKind("A.B.IFoo") == NamespaceRegistry::TypeKind::Interface);
    assert(reg.getTypeKind("a.b.ifoo") == NamespaceRegistry::TypeKind::Interface);

    // Namespace should contain the interface.
    const auto *info = reg.info("a.B");
    assert(info != nullptr);
    assert(info->interfaces.size() == 1);
    assert(info->interfaces.count("A.B.IFoo") == 1);
}

void test_namespace_exists_case_insensitive()
{
    NamespaceRegistry reg;

    reg.registerNamespace("MyNamespace");

    // All case variations should succeed.
    assert(reg.namespaceExists("MyNamespace"));
    assert(reg.namespaceExists("mynamespace"));
    assert(reg.namespaceExists("MYNAMESPACE"));
    assert(reg.namespaceExists("myNAMEspace"));

    // Non-existent namespace should fail.
    assert(!reg.namespaceExists("Other"));
}

void test_type_exists_case_insensitive()
{
    NamespaceRegistry reg;

    reg.registerClass("NS", "MyClass");
    reg.registerInterface("NS", "IMyInterface");

    // Class checks (case-insensitive).
    assert(reg.typeExists("NS.MyClass"));
    assert(reg.typeExists("ns.myclass"));
    assert(reg.typeExists("NS.MYCLASS"));

    // Interface checks (case-insensitive).
    assert(reg.typeExists("NS.IMyInterface"));
    assert(reg.typeExists("ns.imyinterface"));
    assert(reg.typeExists("NS.IMYINTERFACE"));

    // Non-existent type should fail.
    assert(!reg.typeExists("NS.Other"));
}

void test_get_type_kind_positive_negative()
{
    NamespaceRegistry reg;

    reg.registerClass("A", "C1");
    reg.registerInterface("A", "I1");

    // Check class kind.
    assert(reg.getTypeKind("A.C1") == NamespaceRegistry::TypeKind::Class);
    assert(reg.getTypeKind("a.c1") == NamespaceRegistry::TypeKind::Class);

    // Check interface kind.
    assert(reg.getTypeKind("A.I1") == NamespaceRegistry::TypeKind::Interface);
    assert(reg.getTypeKind("a.i1") == NamespaceRegistry::TypeKind::Interface);

    // Non-existent type should return None.
    assert(reg.getTypeKind("A.Missing") == NamespaceRegistry::TypeKind::None);
    assert(reg.getTypeKind("NonExistent.Type") == NamespaceRegistry::TypeKind::None);
}

void test_canonical_spelling_preserved()
{
    NamespaceRegistry reg;

    // First registration uses mixed case.
    reg.registerNamespace("FooBar.BazQux");
    reg.registerClass("FooBar.BazQux", "MyClass");

    // Later registrations use different casings.
    reg.registerNamespace("foobar.bazqux");
    reg.registerClass("FOOBAR.BAZQUX", "AnotherClass");

    // Retrieve info using any casing.
    const auto *info = reg.info("fooBar.bazQUX");
    assert(info != nullptr);

    // Canonical namespace spelling should be the first-seen.
    assert(info->full == "FooBar.BazQux");

    // Both classes should be registered with canonical namespace prefix.
    assert(info->classes.size() == 2);
    assert(info->classes.count("FooBar.BazQux.MyClass") == 1);
    assert(info->classes.count("FooBar.BazQux.AnotherClass") == 1);
}

void test_info_returns_null_for_nonexistent_namespace()
{
    NamespaceRegistry reg;

    reg.registerNamespace("Exists");

    // Existing namespace should return valid pointer.
    assert(reg.info("Exists") != nullptr);
    assert(reg.info("exists") != nullptr);

    // Non-existent namespace should return nullptr.
    assert(reg.info("DoesNotExist") == nullptr);
    assert(reg.info("doesnotexist") == nullptr);
}

void test_register_class_creates_namespace_implicitly()
{
    NamespaceRegistry reg;

    // Register a class without explicitly registering the namespace first.
    reg.registerClass("Implicit.NS", "TestClass");

    // Namespace should now exist.
    assert(reg.namespaceExists("Implicit.NS"));
    assert(reg.namespaceExists("implicit.ns"));

    // Type should exist.
    assert(reg.typeExists("Implicit.NS.TestClass"));
    assert(reg.getTypeKind("implicit.ns.testclass") == NamespaceRegistry::TypeKind::Class);
}

void test_register_interface_creates_namespace_implicitly()
{
    NamespaceRegistry reg;

    // Register an interface without explicitly registering the namespace first.
    reg.registerInterface("Auto.Created", "ITest");

    // Namespace should now exist.
    assert(reg.namespaceExists("Auto.Created"));
    assert(reg.namespaceExists("auto.created"));

    // Type should exist.
    assert(reg.typeExists("Auto.Created.ITest"));
    assert(reg.getTypeKind("auto.created.itest") == NamespaceRegistry::TypeKind::Interface);
}

void test_multiple_types_in_same_namespace()
{
    NamespaceRegistry reg;

    reg.registerNamespace("MyNS");
    reg.registerClass("MyNS", "Class1");
    reg.registerClass("MyNS", "Class2");
    reg.registerInterface("MyNS", "Interface1");
    reg.registerInterface("MyNS", "Interface2");

    const auto *info = reg.info("myns");
    assert(info != nullptr);
    assert(info->classes.size() == 2);
    assert(info->interfaces.size() == 2);

    assert(info->classes.count("MyNS.Class1") == 1);
    assert(info->classes.count("MyNS.Class2") == 1);
    assert(info->interfaces.count("MyNS.Interface1") == 1);
    assert(info->interfaces.count("MyNS.Interface2") == 1);
}

int main()
{
    test_register_namespace_merges_repeated_blocks();
    test_register_class_creates_fq_name();
    test_register_interface_creates_fq_name();
    test_namespace_exists_case_insensitive();
    test_type_exists_case_insensitive();
    test_get_type_kind_positive_negative();
    test_canonical_spelling_preserved();
    test_info_returns_null_for_nonexistent_namespace();
    test_register_class_creates_namespace_implicitly();
    test_register_interface_creates_namespace_implicitly();
    test_multiple_types_in_same_namespace();
    return 0;
}
