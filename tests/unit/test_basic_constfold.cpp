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
#include <cmath>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    auto expectInt = [](const std::string &expr, long long expected)
    {
        std::string src = "10 LET X = " + expr + "\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *ie = dynamic_cast<IntExpr *>(let->expr.get());
        assert(ie && ie->value == expected);
    };

    auto expectFloat = [](const std::string &expr, double expected)
    {
        std::string src = "10 LET X = " + expr + "\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *flt = dynamic_cast<FloatExpr *>(let->expr.get());
        assert(flt && std::fabs(flt->value - expected) < 1e-9);
    };

    auto expectString = [](const std::string &expr, const std::string &expected)
    {
        std::string src = "10 LET A$ = " + expr + "\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *se = dynamic_cast<StringExpr *>(let->expr.get());
        assert(se && se->value == expected);
    };

    // Arithmetic
    expectFloat("1 + 2.5", 3.5);
    expectInt("10 - 3", 7);
    expectInt("6 * 7", 42);
    expectFloat("5 / 2", 2.5);
    expectInt("7 \\ 3", 2);
    expectInt("10 MOD 3", 1);

    // Comparisons
    expectInt("5 = 5", 1);
    expectInt("5 <> 3", 1);
    expectInt("2 < 1", 0);
    expectInt("2 <= 2", 1);
    expectInt("5 > 2", 1);
    expectInt("7 >= 8", 0);

    // Logical operations and unary NOT
    expectInt("0 AND 5", 0);
    expectInt("0 OR 1", 1);
    expectInt("NOT 0", 1);
    expectInt("NOT 4", 0);

    // String operations
    expectString("\"foo\" + \"bar\"", "foobar");
    expectInt("\"foo\" = \"foo\"", 1);
    expectInt("\"foo\" = \"bar\"", 0);
    expectInt("\"a\" <> \"b\"", 1);

    // Rejected string arithmetic retains diagnostic code
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

    return 0;
}
