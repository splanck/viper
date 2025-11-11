// File: tests/unit/test_basic_parse_namespace.cpp
// Purpose: Ensure NAMESPACE parsing captures path segments and nested class.

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src =
        "NAMESPACE A.B\n"
        "  CLASS C\n"
        "    PUBLIC SUB M()\n"
        "    END SUB\n"
        "  END CLASS\n"
        "END NAMESPACE\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog);
    assert(!prog->main.empty());
    auto *ns = dynamic_cast<NamespaceDecl *>(prog->main[0].get());
    assert(ns);
    assert(ns->path.size() == 2);
    assert(ns->path[0] == "A");
    assert(ns->path[1] == "B");
    assert(ns->body.size() == 1);
    auto *cls = dynamic_cast<ClassDecl *>(ns->body[0].get());
    assert(cls);
    assert(cls->name == "C");
    return 0;
}

