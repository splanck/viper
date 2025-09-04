// File: tests/unit/test_basic_constfolder.cpp
// Purpose: Unit tests for BASIC constant folder numeric promotion and string rules.
// Key invariants: Folding respects type promotion and emits existing diagnostics.
// Ownership/Lifetime: Test owns all created objects.
// Links: docs/class-catalog.md

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

static std::unique_ptr<Program> parse(const std::string &src, SourceManager &sm, uint32_t &fid)
{
    fid = sm.addFile("test.bas");
    Parser p(src, fid);
    return p.parseProgram();
}

int main()
{
    // int + float promotes to float
    {
        SourceManager sm;
        uint32_t fid;
        auto prog = parse("10 LET X = 1 + 2.5\n", sm, fid);
        foldConstants(*prog);
        auto *let = dynamic_cast<LetStmt *>(prog->statements[0].get());
        auto *f = dynamic_cast<FloatExpr *>(let->expr.get());
        assert(f && f->value == 3.5);
    }

    // string concatenation
    {
        SourceManager sm;
        uint32_t fid;
        auto prog = parse("10 PRINT \"A\" + \"B\"\n", sm, fid);
        foldConstants(*prog);
        auto *pr = dynamic_cast<PrintStmt *>(prog->statements[0].get());
        auto *s = dynamic_cast<StringExpr *>(pr->items[0].expr.get());
        assert(s && s->value == "AB");
    }

    // rejected string arithmetic keeps diagnostic code
    {
        std::string src = "10 PRINT \"A\" * 2\n";
        SourceManager sm;
        uint32_t fid;
        auto prog = parse(src, sm, fid);
        foldConstants(*prog);
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        std::ostringstream oss;
        em.printAll(oss);
        std::string out = oss.str();
        assert(out.find("error[B2001]") != std::string::npos);
    }

    return 0;
}
