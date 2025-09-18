// File: tests/unit/test_basic_constfold.cpp
// Purpose: Unit tests for BASIC constant folder numeric promotion and string rules.
// Key invariants: Numeric ops promote to float; string concatenation is folded; invalid mixes keep
// diagnostics. Ownership/Lifetime: Test owns all objects locally. Links: docs/class-catalog.md

#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // int + float promotes to float
    {
        std::string src = "10 LET X = 1 + 2.5\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *flt = dynamic_cast<FloatExpr *>(let->expr.get());
        assert(flt && flt->value == 3.5);
    }

    // string concatenation
    {
        std::string src = "10 PRINT \"foo\" + \"bar\"\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *se = dynamic_cast<StringExpr *>(pr->items[0].expr.get());
        assert(se && se->value == "foobar");
    }

    // rejected string arithmetic retains diagnostic code
    {
        std::string src = "10 PRINT \"a\" * \"b\"\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        std::ostringstream oss;
        em.printAll(oss);
        assert(em.errorCount() == 1);
        assert(oss.str().find("B2001") != std::string::npos);
    }

    // numeric comparison
    {
        std::string src = "10 LET X = 5 > 2\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(let->expr.get());
        assert(ie && ie->value == 1);
    }

    // string comparison
    {
        std::string src = "10 PRINT \"foo\" = \"bar\"\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(pr->items[0].expr.get());
        assert(ie && ie->value == 0);
    }

    // logical OR
    {
        std::string src = "10 LET X = 0 OR 1\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(let->expr.get());
        assert(ie && ie->value == 1);
    }

    // numeric modulus
    {
        std::string src = "10 LET X = 7 MOD 3\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(let->expr.get());
        assert(ie && ie->value == 1);
    }

    // string inequality
    {
        std::string src = "10 PRINT \"foo\" <> \"bar\"\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(pr->items[0].expr.get());
        assert(ie && ie->value == 1);
    }

    // boolean literals stay BOOLEAN after folding
    {
        std::string src =
            "10 LET A = NOT TRUE\n"
            "20 LET B = TRUE AND TRUE\n"
            "30 LET C = FALSE ORELSE TRUE\n"
            "40 LET D = FALSE ORELSE (1 = 1)\n"
            "50 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("bool.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *letA = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *boolA = dynamic_cast<BoolExpr *>(letA->expr.get());
        assert(boolA && boolA->value == false);
        auto *letB = dynamic_cast<LetStmt *>(prog->main[1].get());
        auto *boolB = dynamic_cast<BoolExpr *>(letB->expr.get());
        assert(boolB && boolB->value == true);
        auto *letC = dynamic_cast<LetStmt *>(prog->main[2].get());
        auto *boolC = dynamic_cast<BoolExpr *>(letC->expr.get());
        assert(boolC && boolC->value == true);
        auto *letD = dynamic_cast<LetStmt *>(prog->main[3].get());
        auto *intD = dynamic_cast<IntExpr *>(letD->expr.get());
        assert(intD && intD->value == 1);
    }

    return 0;
}
