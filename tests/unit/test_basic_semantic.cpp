// File: tests/unit/test_basic_semantic.cpp
// Purpose: Unit test verifying BASIC semantic analyzer and lowerer handle a
//          representative AST without diagnostics.
// Key invariants: Analyzer collects symbols/labels and lowering produces
//                 functions for user procedures and main.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src =
        "100 FUNCTION F(N)\n"
        "110 RETURN N + 1\n"
        "120 END FUNCTION\n"
        "200 SUB P(Q())\n"
        "210 PRINT LEN(\"SUB\")\n"
        "220 END SUB\n"
        "1000 DIM A(5)\n"
        "1010 DIM FLAG AS BOOLEAN\n"
        "1020 DIM S$\n"
        "1030 LET FLAG = TRUE\n"
        "1035 LET FLAG = NOT FLAG\n"
        "1040 LET X = 3\n"
        "1050 LET Y# = 1.5\n"
        "1060 RANDOMIZE 42: PRINT LEN(\"HI\"), A(X)\n"
        "1070 IF FLAG THEN LET X = X + 1 ELSEIF X > 1 THEN LET X = X - 1 ELSE PRINT \"ZERO\": PRINT \"TAIL\"\n"
        "1080 WHILE X > 0\n"
        "1090 PRINT LEN(\"HI\"), A(X)\n"
        "1100 LET X = X - 1: PRINT X\n"
        "1110 WEND\n"
        "1120 FOR I = 1 TO 3\n"
        "1130 LET A(I) = I\n"
        "1140 NEXT I\n"
        "1150 INPUT \"Value?\", S$\n"
        "1160 PRINT F(X)\n"
        "1170 GOTO 2000\n"
        "1180 END\n"
        "2000 PRINT \"DONE\";\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();

    DiagnosticEngine de;
    DiagnosticEmitter em(de, sm);
    em.addSource(fid, src);
    SemanticAnalyzer sema(em);
    sema.analyze(*prog);
    assert(em.errorCount() == 0);
    assert(em.warningCount() == 0);
    assert(sema.symbols().count("A") == 1);
    assert(sema.symbols().count("FLAG") == 1);
    assert(sema.symbols().count("S$") == 1);
    assert(sema.symbols().count("X") == 1);
    assert(sema.symbols().count("Y#") == 1);
    assert(sema.symbols().count("I") == 1);
    assert(sema.labels().count(1000) == 1);
    assert(sema.labels().count(1070) == 1);
    assert(sema.labels().count(2000) == 1);
    assert(sema.labelRefs().count(2000) == 1);
    assert(sema.procs().count("F") == 1);
    assert(sema.procs().count("P") == 1);
    assert(sema.procs().at("F").params.size() == 1);
    assert(sema.procs().at("P").params.size() == 1);

    bool hasStmtList = false;
    for (const auto &stmt : prog->main)
    {
        if (dynamic_cast<const StmtList *>(stmt.get()))
        {
            hasStmtList = true;
            break;
        }
    }
    assert(hasStmtList);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);
    bool sawMain = false;
    bool sawFunction = false;
    bool sawSub = false;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
            sawMain = true;
        else if (fn.name == "F")
            sawFunction = true;
        else if (fn.name == "P")
            sawSub = true;
    }
    assert(sawMain && sawFunction && sawSub);
    return 0;
}
