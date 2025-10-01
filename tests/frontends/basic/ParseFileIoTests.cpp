// File: tests/frontends/basic/ParseFileIoTests.cpp
// Purpose: Validate parsing of BASIC OPEN/CLOSE statements for file I/O.
// Key invariants: AST printer reflects mode enum numeric values and fields.
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
    uint32_t fid = sm.addFile("fileio.bas");
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
        std::string dump =
            dumpProgram("10 OPEN \"foo.txt\" FOR INPUT AS #1\n20 END\n");
        assert(dump ==
               "10: (OPEN mode=INPUT(0) path=\"foo.txt\" channel=#1)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 CLOSE #1\n20 END\n");
        assert(dump == "10: (CLOSE channel=#1)\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 PRINT #1, X, Y\n20 END\n");
        assert(dump == "10: (PRINT# channel=#1 args=[X Y])\n20: (END)\n");
    }

    {
        std::string dump = dumpProgram("10 LINE INPUT #1, A$\n20 END\n");
        assert(dump == "10: (LINE-INPUT# channel=#1 target=A$)\n20: (END)\n");
    }
}
