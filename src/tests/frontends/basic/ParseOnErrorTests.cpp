// File: tests/frontends/basic/ParseOnErrorTests.cpp
// Purpose: Validate parsing of ON ERROR and RESUME statements in BASIC front-end.
// Key invariants: AST printer output reflects statement variants precisely.
// Ownership/Lifetime: Test owns parser and AST instances.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
std::string dumpProgram(const std::string &src)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("onerror.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);
    AstPrinter printer;
    return printer.dump(*prog);
}
} // namespace

int main()
{
    {
        std::string dump = dumpProgram("10 ON ERROR GOTO 200\n20 END\n");
        assert(dump == "10: (ON-ERROR GOTO 200)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 ON ERROR GOTO 0\n20 END\n");
        assert(dump == "10: (ON-ERROR GOTO 0)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 RESUME\n20 END\n");
        assert(dump == "10: (RESUME)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 RESUME NEXT\n20 END\n");
        assert(dump == "10: (RESUME NEXT)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 RESUME 400\n20 END\n");
        assert(dump == "10: (RESUME 400)\n20: (END)\n");
    }
}
