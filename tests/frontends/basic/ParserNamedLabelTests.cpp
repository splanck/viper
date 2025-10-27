// File: tests/frontends/basic/ParserNamedLabelTests.cpp
// Purpose: Verify the BASIC parser recognises named labels and enforces uniqueness.
// Key invariants: Named labels are mapped to synthetic numeric identifiers and duplicate
//                 definitions emit diagnostics.
// Ownership/Lifetime: Tests instantiate parser and diagnostic infrastructure locally.
// Links: docs/codemap.md

#include "frontends/basic/AST.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        const std::string src = "Start:\nRun: PRINT 1\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("named_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);

        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() == 0);
        assert(program->main.size() == 2);
        auto *first = dynamic_cast<LabelStmt *>(program->main[0].get());
        assert(first != nullptr);
        int startLine = first->line;
        assert(startLine > 0);
        auto *second = dynamic_cast<PrintStmt *>(program->main[1].get());
        assert(second != nullptr);
        int runLine = program->main[1]->line;
        assert(runLine > 0);
        assert(runLine != startLine);
    }

    {
        const std::string src = "Start:\nStart: PRINT 1\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("duplicate_named_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);

        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() == 1);
    }

    return 0;
}
