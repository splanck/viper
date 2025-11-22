//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_parse_using.cpp
// Purpose: Ensure USING directive parsing captures namespace paths and aliases. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

void test_simple_using()
{
    std::string src = "USING Foo.Bar\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(!prog->main.empty());
    auto *u = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u);
    assert(u->namespacePath.size() == 2);
    assert(u->namespacePath[0] == "FOO");
    assert(u->namespacePath[1] == "BAR");
    assert(u->alias.empty());
}

void test_using_with_alias()
{
    std::string src = "USING FB = Foo.Bar.Baz\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(!prog->main.empty());
    auto *u = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u);
    assert(u->namespacePath.size() == 3);
    assert(u->namespacePath[0] == "FOO");
    assert(u->namespacePath[1] == "BAR");
    assert(u->namespacePath[2] == "BAZ");
    assert(u->alias == "FB");
}

void test_multiple_usings()
{
    std::string src = "USING System\n"
                      "USING FB = Foo.Bar\n"
                      "USING A.B.C.D\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(prog->main.size() == 3);

    auto *u1 = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u1);
    assert(u1->namespacePath.size() == 1);
    assert(u1->namespacePath[0] == "SYSTEM");
    assert(u1->alias.empty());

    auto *u2 = dynamic_cast<UsingDecl *>(prog->main[1].get());
    assert(u2);
    assert(u2->namespacePath.size() == 2);
    assert(u2->namespacePath[0] == "FOO");
    assert(u2->namespacePath[1] == "BAR");
    assert(u2->alias == "FB");

    auto *u3 = dynamic_cast<UsingDecl *>(prog->main[2].get());
    assert(u3);
    assert(u3->namespacePath.size() == 4);
    assert(u3->namespacePath[0] == "A");
    assert(u3->namespacePath[1] == "B");
    assert(u3->namespacePath[2] == "C");
    assert(u3->namespacePath[3] == "D");
    assert(u3->alias.empty());
}

void test_using_trailing_dot_recovers()
{
    // Malformed: trailing dot; parser should recover and build a node.
    std::string src = "USING Foo.Bar.\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(!prog->main.empty());
    auto *u = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u);
    // Parser stops at the trailing dot; path should still contain FOO.BAR.
    assert(u->namespacePath.size() == 2);
    assert(u->namespacePath[0] == "FOO");
    assert(u->namespacePath[1] == "BAR");
}

void test_using_with_statement()
{
    // USING followed by other statements.
    std::string src = "USING Foo\n"
                      "PRINT 42\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(prog->main.size() == 2);
    auto *u = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u);
    assert(u->namespacePath.size() == 1);
    assert(u->namespacePath[0] == "FOO");
}

void test_single_segment_namespace()
{
    std::string src = "USING System\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(!prog->main.empty());
    auto *u = dynamic_cast<UsingDecl *>(prog->main[0].get());
    assert(u);
    assert(u->namespacePath.size() == 1);
    assert(u->namespacePath[0] == "SYSTEM");
    assert(u->alias.empty());
}

int main()
{
    test_simple_using();
    test_using_with_alias();
    test_multiple_usings();
    test_using_trailing_dot_recovers();
    test_using_with_statement();
    test_single_segment_namespace();
    return 0;
}
