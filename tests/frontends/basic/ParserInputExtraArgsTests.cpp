// File: tests/frontends/basic/ParserInputExtraArgsTests.cpp
// Purpose: Verify INPUT parser emits diagnostics for unsupported extra variables.
// Key invariants: Parser should report B0101 once and continue parsing subsequent statements.
// Ownership/Lifetime: Test owns parser/emitter instances and inspects AST and diagnostics.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 INPUT A, B\n20 PRINT A\n30 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("input.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);
    assert(program->main.size() == 3);
    assert(dynamic_cast<InputStmt *>(program->main[0].get()));
    assert(dynamic_cast<PrintStmt *>(program->main[1].get()));
    assert(dynamic_cast<EndStmt *>(program->main[2].get()));

    assert(emitter.errorCount() == 1);

    std::ostringstream oss;
    emitter.printAll(oss);
    std::string output = oss.str();
    assert(output.find("error[B0101]") != std::string::npos);
    assert(output.find(
               "INPUT currently supports a single variable; extra items will be ignored") != std::string::npos);

    return 0;
}
