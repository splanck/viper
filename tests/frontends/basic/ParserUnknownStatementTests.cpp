// File: tests/frontends/basic/ParserUnknownStatementTests.cpp
// Purpose: Ensure BASIC parser reports diagnostics for unknown statement keywords.
// Key invariants: Parser emits B0001 and skips to end-of-line for unrecognized statements.
// Ownership/Lifetime: Test owns parser/emitter instances and inspects resulting AST/diagnostics.
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
    std::string src = "10 FOOBAR 200: PRINT 5\n20 PRINT 1\n30 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("unknown.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);
    assert(program->main.size() == 3);
    auto *label = dynamic_cast<LabelStmt *>(program->main[0].get());
    assert(label);
    assert(label->line == 10);
    assert(label->loc.isValid());
    assert(dynamic_cast<PrintStmt *>(program->main[1].get()));
    assert(dynamic_cast<EndStmt *>(program->main[2].get()));

    assert(emitter.errorCount() == 1);

    std::ostringstream oss;
    emitter.printAll(oss);
    std::string output = oss.str();
    assert(output.find("error[B0001]") != std::string::npos);
    assert(output.find("unknown statement 'FOOBAR'") != std::string::npos);

    return 0;
}
