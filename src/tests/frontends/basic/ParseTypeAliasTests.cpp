//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParseTypeAliasTests.cpp
// Purpose: Verify BASIC parser recognises type aliases like INT in AS clauses. 
// Key invariants: `parseTypeKeyword` normalises identifier spellings before mapping.
// Ownership/Lifetime: Test constructs parser instances per scenario; no shared state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        const std::string src = "CLASS A\n"
                                "  n AS INT\n"
                                "END CLASS\n"
                                "END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("class_type_alias.bas");

        Parser parser(src, fid);
        auto program = parser.parseProgram();
        assert(program);
        assert(program->main.size() == 2);

        auto *classDecl = dynamic_cast<ClassDecl *>(program->main[0].get());
        assert(classDecl);
        assert(classDecl->fields.size() == 1);
        assert(classDecl->fields[0].name == "N");
        assert(classDecl->fields[0].type == Type::I64);
    }

    {
        const std::string src = "FUNCTION Foo(x AS int)\n"
                                "  Foo = 0\n"
                                "END FUNCTION\n"
                                "END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("function_param_type_alias.bas");

        Parser parser(src, fid);
        auto program = parser.parseProgram();
        assert(program);
        assert(program->procs.size() == 1);

        auto *fn = dynamic_cast<FunctionDecl *>(program->procs[0].get());
        assert(fn);
        assert(fn->params.size() == 1);
        assert(fn->params[0].name == "X");
        assert(fn->params[0].type == Type::I64);
    }

    return 0;
}
