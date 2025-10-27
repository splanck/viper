// File: tests/frontends/basic/ParseGotoTests.cpp
// Purpose: Validate parsing and AST printing of BASIC GOTO statements.
// Key invariants: GOTO statements accept numeric or named targets.
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
    std::string dump = dumpProgram("10 GOTO 200\n20 END\n");
    assert(dump == "10: (GOTO 200)\n20: (END)\n");

    std::string labelDump = dumpProgram("10 GOTO Start\n20 END\nStart:\n30 END\n");
    assert(labelDump == "10: (GOTO 1000000)\n20: (END)\n1000000: (LABEL)\n30: (END)\n");
    return 0;
}
