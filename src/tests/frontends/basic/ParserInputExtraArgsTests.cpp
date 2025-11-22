//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParserInputExtraArgsTests.cpp
// Purpose: Verify INPUT parser accepts comma-separated variable lists. 
// Key invariants: Parsed INPUT statement records each variable without diagnostics.
// Ownership/Lifetime: Test owns parser/emitter instances and inspects AST and diagnostics.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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

    auto *inputStmt = dynamic_cast<InputStmt *>(program->main[0].get());
    assert(inputStmt);
    assert(inputStmt->vars.size() == 2);
    assert(inputStmt->vars[0] == "A");
    assert(inputStmt->vars[1] == "B");

    assert(emitter.errorCount() == 0);

    return 0;
}
