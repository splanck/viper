// File: tests/frontends/basic/ParseGosubTests.cpp
// Purpose: Validate parsing of BASIC GOSUB statements and AST printing.
// Key invariants: Parsed GOSUB nodes record target line numbers for semantics.
// Ownership/Lifetime: Test owns parser and AST artifacts.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("gosub.bas");

    {
        std::string src = "30 GOSUB 200\n40 END\n";
        Parser parser(src, fid);
        auto program = parser.parseProgram();
        assert(program);
        assert(program->main.size() == 2);
        auto *gosub = dynamic_cast<GosubStmt *>(program->main[0].get());
        assert(gosub);
        assert(gosub->targetLine == 200);

        AstPrinter printer;
        std::string dump = printer.dump(*program);
        assert(dump == "30: (GOSUB 200)\n40: (END)\n");
    }

    return 0;
}
