// File: tests/unit/test_basic_constfold.cpp
// Purpose: Unit tests for BASIC constant folder numeric promotion and string rules.
// Key invariants: Numeric ops promote to float; string concatenation is folded; invalid mixes keep
// diagnostics. Ownership/Lifetime: Test owns all objects locally. Links: docs/codemap.md

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
        auto *be = dynamic_cast<BoolExpr *>(let->expr.get());
        assert(be && be->value == true);
    }

    // INTEGER overflow prevents folding (32767 + 1)
    {
        std::string src = "10 LET X = 32767 + 1\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("overflow.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        assert(let);
        auto *bin = dynamic_cast<BinaryExpr *>(let->expr.get());
        assert(bin && bin->op == BinaryExpr::Op::Add);
    }

    // LONG + DOUBLE promotes to DOUBLE
    {
        std::string src = "10 LET X = 2147483647 + 2#\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("mixed_double.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *flt = dynamic_cast<FloatExpr *>(let->expr.get());
        assert(flt && flt->value == 2147483649.0);
    }

    // SINGLE + INTEGER promotes to floating result
    {
        std::string src = "10 LET X = 1! + 2\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("mixed_single.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *flt = dynamic_cast<FloatExpr *>(let->expr.get());
        assert(flt && flt->value == 3.0);
    }

    // Division by zero is not folded
    {
        std::string src = "10 LET X = 10 / 0\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("div_zero.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *bin = dynamic_cast<BinaryExpr *>(let->expr.get());
        assert(bin && bin->op == BinaryExpr::Op::Div);
    }

    // Modulo by zero is not folded
    {
        std::string src = "10 LET X = 10 MOD 0\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("mod_zero.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *bin = dynamic_cast<BinaryExpr *>(let->expr.get());
        assert(bin && bin->op == BinaryExpr::Op::Mod);
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
        auto *be = dynamic_cast<BoolExpr *>(pr->items[0].expr.get());
        assert(be && be->value == false);
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
        auto *be = dynamic_cast<BoolExpr *>(let->expr.get());
        assert(be && be->value == true);
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
        auto *be = dynamic_cast<BoolExpr *>(pr->items[0].expr.get());
        assert(be && be->value == true);
    }

    // LEN on literal string
    {
        std::string src = "10 PRINT LEN(\"abc\")\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("len.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(pr->items[0].expr.get());
        assert(ie && ie->value == 3);
    }

    // LEN handles escape sequences decoded to literal characters
    {
        std::string src = "10 PRINT LEN(\"\\n\")\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("len_escape.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(pr->items[0].expr.get());
        assert(ie && ie->value == 1);
    }

    // MID$ clamps indices and handles unicode source
    {
        std::string src = "10 PRINT MID$(\"AßC\", 0, 5)\n20 PRINT MID$(\"xyz\", 10, 2)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("mid.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *first = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *mid1 = dynamic_cast<StringExpr *>(first->items[0].expr.get());
        assert(mid1 && mid1->value == "AßC");
        auto *second = dynamic_cast<PrintStmt *>(prog->main[1].get());
        auto *mid2 = dynamic_cast<StringExpr *>(second->items[0].expr.get());
        assert(mid2 && mid2->value.empty());
    }

    // LEFT$ handles empty strings and negative counts
    {
        std::string src = "10 PRINT LEFT$(\"abc\", -1)\n20 PRINT LEFT$(\"\", 5)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("left.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *first = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *left1 = dynamic_cast<StringExpr *>(first->items[0].expr.get());
        assert(left1 && left1->value.empty());
        auto *second = dynamic_cast<PrintStmt *>(prog->main[1].get());
        auto *left2 = dynamic_cast<StringExpr *>(second->items[0].expr.get());
        assert(left2 && left2->value.empty());
    }

    // RIGHT$ trims suffix with unicode characters
    {
        std::string src = "10 PRINT RIGHT$(\"ñab\", 2)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("right.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->main[0].get());
        auto *se = dynamic_cast<StringExpr *>(pr->items[0].expr.get());
        assert(se && se->value == "ab");
    }

    // boolean literals stay BOOLEAN after folding
    {
        std::string src = "10 LET A = NOT TRUE\n"
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
        auto *boolD = dynamic_cast<BoolExpr *>(letD->expr.get());
        assert(boolD && boolD->value == true);
    }

    return 0;
}
