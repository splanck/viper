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

    {
        std::string src2 =
            "10 SUB Greet(name)\n20 PRINT \"hi\"\n30 END SUB\n40 Greet \"Alice\"\n50 END\n";

        SourceManager sm2;
        uint32_t fid2 = sm2.addFile("missing_paren.bas");

        DiagnosticEngine de2;
        DiagnosticEmitter emitter2(de2, sm2);
        emitter2.addSource(fid2, src2);

        Parser parser2(src2, fid2, &emitter2);
        auto program2 = parser2.parseProgram();
        assert(program2);
        assert(emitter2.errorCount() == 1);

        std::ostringstream oss2;
        emitter2.printAll(oss2);
        std::string output2 = oss2.str();
        assert(output2.find("error[B0001]") != std::string::npos);
        assert(output2.find("expected '(") != std::string::npos);
        assert(output2.find("procedure name 'GREET'") != std::string::npos);
        auto linePos = output2.find("40 Greet \"Alice\"");
        assert(linePos != std::string::npos);
        auto caretSnippet = output2.find("\"Alice\"\n         ^", linePos);
        assert(caretSnippet != std::string::npos);
    }

    return 0;
}
