// File: tests/unit/test_ns_registry_builder.cpp
// Purpose: Test buildNamespaceRegistry populates registry from AST.

#include "frontends/basic/sem/RegistryBuilder.hpp"
#include "frontends/basic/sem/NamespaceRegistry.hpp"
#include "frontends/basic/sem/UsingContext.hpp"
#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include <cassert>
#include <memory>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

// Helper to create a test program
Program makeProgram(std::vector<StmtPtr> stmts)
{
    Program prog;
    prog.main = std::move(stmts);
    return prog;
}

void test_empty_program()
{
    NamespaceRegistry reg;
    UsingContext uc;
    Program prog;

    buildNamespaceRegistry(prog, reg, uc, nullptr);

    // Registry should be empty (no registrations).
    assert(uc.imports().empty());
}

void test_single_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto ns = std::make_unique<NamespaceDecl>();
    ns->path = {"MyNamespace"};
    ns->loc = SourceLoc{1, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("MyNamespace"));
}

void test_nested_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto ns = std::make_unique<NamespaceDecl>();
    ns->path = {"A", "B", "C"};
    ns->loc = SourceLoc{1, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("A.B.C"));
}

void test_class_in_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto klass = std::make_unique<ClassDecl>();
    klass->name = "MyClass";
    klass->loc = SourceLoc{1, 1, 1};

    auto ns = std::make_unique<NamespaceDecl>();
    ns->path = {"MyNamespace"};
    ns->loc = SourceLoc{1, 1, 1};
    ns->body.push_back(std::move(klass));

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("MyNamespace"));
    assert(reg.typeExists("MyNamespace.MyClass"));
    assert(reg.getTypeKind("MyNamespace.MyClass") == NamespaceRegistry::TypeKind::Class);
}

void test_interface_in_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto iface = std::make_unique<InterfaceDecl>();
    iface->qualifiedName = {"MyNamespace", "IFoo"};
    iface->loc = SourceLoc{1, 1, 1};

    auto ns = std::make_unique<NamespaceDecl>();
    ns->path = {"MyNamespace"};
    ns->loc = SourceLoc{1, 1, 1};
    ns->body.push_back(std::move(iface));

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("MyNamespace"));
    assert(reg.typeExists("MyNamespace.IFoo"));
    assert(reg.getTypeKind("MyNamespace.IFoo") == NamespaceRegistry::TypeKind::Interface);
}

void test_multiple_namespaces()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto ns1 = std::make_unique<NamespaceDecl>();
    ns1->path = {"NS1"};
    ns1->loc = SourceLoc{1, 1, 1};

    auto ns2 = std::make_unique<NamespaceDecl>();
    ns2->path = {"NS2"};
    ns2->loc = SourceLoc{2, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns1));
    stmts.push_back(std::move(ns2));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("NS1"));
    assert(reg.namespaceExists("NS2"));
}

void test_merged_namespace()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // First declaration of MyNS with ClassA
    auto klassA = std::make_unique<ClassDecl>();
    klassA->name = "ClassA";
    klassA->loc = SourceLoc{1, 1, 1};

    auto ns1 = std::make_unique<NamespaceDecl>();
    ns1->path = {"MyNS"};
    ns1->loc = SourceLoc{1, 1, 1};
    ns1->body.push_back(std::move(klassA));

    // Second declaration of MyNS with ClassB
    auto klassB = std::make_unique<ClassDecl>();
    klassB->name = "ClassB";
    klassB->loc = SourceLoc{2, 1, 1};

    auto ns2 = std::make_unique<NamespaceDecl>();
    ns2->path = {"MyNS"};
    ns2->loc = SourceLoc{2, 1, 1};
    ns2->body.push_back(std::move(klassB));

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(ns1));
    stmts.push_back(std::move(ns2));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("MyNS"));
    assert(reg.typeExists("MyNS.ClassA"));
    assert(reg.typeExists("MyNS.ClassB"));
}

void test_using_directive()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto usingDecl = std::make_unique<UsingDecl>();
    usingDecl->namespacePath = {"System", "Collections"};
    usingDecl->alias = "";
    usingDecl->loc = SourceLoc{1, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(usingDecl));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(uc.imports().size() == 1);
    assert(uc.imports()[0].ns == "System.Collections");
    assert(uc.imports()[0].alias.empty());
}

void test_using_directive_with_alias()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto usingDecl = std::make_unique<UsingDecl>();
    usingDecl->namespacePath = {"System", "Collections"};
    usingDecl->alias = "SC";
    usingDecl->loc = SourceLoc{1, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(usingDecl));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(uc.imports().size() == 1);
    assert(uc.imports()[0].ns == "System.Collections");
    assert(uc.imports()[0].alias == "SC");
    assert(uc.hasAlias("SC"));
    assert(uc.resolveAlias("SC") == "System.Collections");
}

void test_multiple_using_directives()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto using1 = std::make_unique<UsingDecl>();
    using1->namespacePath = {"NS1"};
    using1->alias = "";
    using1->loc = SourceLoc{1, 1, 1};

    auto using2 = std::make_unique<UsingDecl>();
    using2->namespacePath = {"NS2"};
    using2->alias = "";
    using2->loc = SourceLoc{2, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(using1));
    stmts.push_back(std::move(using2));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(uc.imports().size() == 2);
    assert(uc.imports()[0].ns == "NS1");
    assert(uc.imports()[1].ns == "NS2");
}

void test_global_class()
{
    NamespaceRegistry reg;
    UsingContext uc;

    auto klass = std::make_unique<ClassDecl>();
    klass->name = "GlobalClass";
    klass->loc = SourceLoc{1, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(klass));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    // Global classes have empty namespace prefix
    assert(reg.typeExists("GlobalClass"));
    assert(reg.getTypeKind("GlobalClass") == NamespaceRegistry::TypeKind::Class);
}

void test_complex_nested_structure()
{
    NamespaceRegistry reg;
    UsingContext uc;

    // Build: NAMESPACE A.B { CLASS C }
    auto klassC = std::make_unique<ClassDecl>();
    klassC->name = "C";
    klassC->loc = SourceLoc{1, 1, 1};

    auto nsAB = std::make_unique<NamespaceDecl>();
    nsAB->path = {"A", "B"};
    nsAB->loc = SourceLoc{1, 1, 1};
    nsAB->body.push_back(std::move(klassC));

    // Build: USING A.B AS AB
    auto usingDecl = std::make_unique<UsingDecl>();
    usingDecl->namespacePath = {"A", "B"};
    usingDecl->alias = "AB";
    usingDecl->loc = SourceLoc{2, 1, 1};

    std::vector<StmtPtr> stmts;
    stmts.push_back(std::move(nsAB));
    stmts.push_back(std::move(usingDecl));

    Program prog = makeProgram(std::move(stmts));
    buildNamespaceRegistry(prog, reg, uc, nullptr);

    assert(reg.namespaceExists("A.B"));
    assert(reg.typeExists("A.B.C"));
    assert(uc.imports().size() == 1);
    assert(uc.hasAlias("AB"));
    assert(uc.resolveAlias("AB") == "A.B");
}

int main()
{
    test_empty_program();
    test_single_namespace();
    test_nested_namespace();
    test_class_in_namespace();
    test_interface_in_namespace();
    test_multiple_namespaces();
    test_merged_namespace();
    test_using_directive();
    test_using_directive_with_alias();
    test_multiple_using_directives();
    test_global_class();
    test_complex_nested_structure();
    return 0;
}
