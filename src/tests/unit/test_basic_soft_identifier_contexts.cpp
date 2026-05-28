//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_soft_identifier_contexts.cpp
// Purpose: Regression coverage for contextual BASIC keywords used as identifiers.
// Key invariants: Soft identifiers parse consistently across declarations, IO,
//                 arrays, bounds intrinsics, and loop syntax.
// Ownership/Lifetime: Test owns parser and semantic analyzer state.
// Links: docs/basic-reference.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main() {
    const std::string src = "10 CONST BASE = 7\n"
                            "20 DIM COLOR(2)\n"
                            "30 DIM COS AS STRING\n"
                            "40 REDIM COLOR(3)\n"
                            "50 LET FLOOR = LBOUND(COLOR)\n"
                            "60 INPUT COS\n"
                            "70 INPUT #1, RANDOM\n"
                            "80 LINE INPUT \"Name? \", APPEND\n"
                            "90 SUB TOUCH(POW AS LONG, NEXT() AS LONG)\n"
                            "100 STATIC RANDOM AS LONG\n"
                            "110 SHARED COLOR, BASE\n"
                            "120 FOR EACH APPEND IN COLOR\n"
                            "130 PRINT APPEND\n"
                            "140 NEXT APPEND\n"
                            "150 FOR BASE = 0 TO 1\n"
                            "160 LET COLOR(BASE) = BASE\n"
                            "170 NEXT BASE\n"
                            "180 END SUB\n"
                            "190 END\n";

    SourceManager sm;
    const uint32_t fid = sm.addFile("soft_identifiers.bas");
    DiagnosticEngine parseDiags;
    DiagnosticEmitter parseEmitter(parseDiags, sm);
    parseEmitter.addSource(fid, src);

    Parser parser(src, fid, &parseEmitter);
    auto prog = parser.parseProgram();
    assert(prog);
    if (parseEmitter.errorCount() != 0) {
        std::ostringstream oss;
        parseEmitter.printAll(oss);
        std::cerr << oss.str();
    }
    assert(parseEmitter.errorCount() == 0);

    assert(prog->main.size() >= 9);
    auto *constant = dynamic_cast<ConstStmt *>(prog->main[0].get());
    assert(constant && constant->name == "BASE");

    auto *redim = dynamic_cast<ReDimStmt *>(prog->main[3].get());
    assert(redim && redim->name == "COLOR");

    auto *let = dynamic_cast<LetStmt *>(prog->main[4].get());
    assert(let);
    auto *bound = dynamic_cast<LBoundExpr *>(let->expr.get());
    assert(bound && bound->name == "COLOR");

    auto *input = dynamic_cast<InputStmt *>(prog->main[5].get());
    assert(input && input->vars.size() == 1 && input->vars.front() == "COS");

    auto *inputCh = dynamic_cast<InputChStmt *>(prog->main[6].get());
    assert(inputCh && inputCh->targets.size() == 1 && inputCh->targets.front().name == "RANDOM");

    auto *lineInput = dynamic_cast<InputStmt *>(prog->main[7].get());
    assert(lineInput && lineInput->prompt && lineInput->vars.size() == 1 &&
           lineInput->vars.front() == "APPEND");

    assert(prog->procs.size() == 1);
    auto *sub = dynamic_cast<SubDecl *>(prog->procs.front().get());
    assert(sub && sub->params.size() == 2);
    assert(sub->params[0].name == "POW");
    assert(sub->params[1].name == "NEXT" && sub->params[1].is_array);
    assert(sub->body.size() >= 4);
    assert(dynamic_cast<StaticStmt *>(sub->body[0].get()));
    auto *shared = dynamic_cast<SharedStmt *>(sub->body[1].get());
    assert(shared && shared->names.size() == 2 && shared->names[0] == "COLOR" &&
           shared->names[1] == "BASE");
    auto *forEach = dynamic_cast<ForEachStmt *>(sub->body[2].get());
    assert(forEach && forEach->elementVar == "APPEND" && forEach->arrayName == "COLOR");
    auto *forStmt = dynamic_cast<ForStmt *>(sub->body[3].get());
    assert(forStmt && dynamic_cast<VarExpr *>(forStmt->varExpr.get()));

    DiagnosticEngine semaDiags;
    DiagnosticEmitter semaEmitter(semaDiags, sm);
    semaEmitter.addSource(fid, src);
    SemanticAnalyzer analyzer(semaEmitter);
    analyzer.analyze(*prog);
    if (semaEmitter.errorCount() != 0) {
        std::ostringstream oss;
        semaEmitter.printAll(oss);
        std::cerr << oss.str();
    }
    assert(semaEmitter.errorCount() == 0);

    return 0;
}
