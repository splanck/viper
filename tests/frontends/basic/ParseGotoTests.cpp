// File: tests/frontends/basic/ParseGotoTests.cpp
// Purpose: Validate parsing and AST printing of BASIC GOTO statements.
// Key invariants: GOTO resolves the destination to a numeric label for the AST printer.
// Ownership/Lifetime: Test owns parser, AST, and source manager instances.
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
    uint32_t fid = sm.addFile("goto.bas");
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
        std::string dump = dumpProgram("10 GOTO 200\n20 END\n");
        assert(dump == "10: (GOTO 200)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram(
            "10 GOTO Handler\n20 END\nHandler: RETURN\n");
        assert(dump == "10: (GOTO 1000000)\n20: (END)\n1000000: (RETURN)\n");
    }

    return 0;
}
