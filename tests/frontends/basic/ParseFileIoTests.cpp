// File: tests/frontends/basic/ParseFileIoTests.cpp
// Purpose: Validate parsing of BASIC OPEN/CLOSE statements for file I/O.
// Key invariants: AST printer reflects mode enum numeric values and fields.
// Ownership/Lifetime: Test owns parser and AST instances.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>
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
        std::string dump = dumpProgram("10 OPEN \"foo.txt\" FOR INPUT AS #1\n20 END\n");
        assert(dump == "10: (OPEN mode=INPUT(0) path=\"foo.txt\" channel=#1)\n20: (END)\n");
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

    {
        const std::string src = "10 DIM BUF(4)\n"
                                "20 LINE INPUT #1, BUF(2)\n"
                                "30 END\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("line_input_array.bas");
        Parser parser(src, fid);
        auto program = parser.parseProgram();
        assert(program);
        assert(program->main.size() >= 2);
        auto *lineInput = dynamic_cast<LineInputChStmt *>(program->main[1].get());
        assert(lineInput);
        assert(lineInput->targetVar);
        assert(dynamic_cast<ArrayExpr *>(lineInput->targetVar.get()));
    }

    {
        const std::string src = "10 LINE INPUT #1, LEFT$(A$, 1)\n"
                                "20 END\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("line_input_invalid.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() >= 1);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        assert(output.find("expected variable") != std::string::npos);
    }
}
