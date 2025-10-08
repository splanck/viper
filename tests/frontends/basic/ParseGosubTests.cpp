// File: tests/frontends/basic/ParseGosubTests.cpp
// Purpose: Validate parsing and AST printing of BASIC GOSUB statements.
// Key invariants: GOSUB requires a numeric target line and prints via AstPrinter.
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
    uint32_t fid = sm.addFile("gosub.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);
    AstPrinter printer;
    return printer.dump(*prog);
}
} // namespace

int main()
{
    std::string dump = dumpProgram("10 GOSUB 200\n20 END\n");
    assert(dump == "10: (GOSUB 200)\n20: (END)\n");
    return 0;
}
