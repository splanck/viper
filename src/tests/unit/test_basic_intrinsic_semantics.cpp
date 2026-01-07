//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_intrinsic_semantics.cpp
// Purpose: Ensure semantic analyzer reports descriptive intrinsic diagnostics.
// Key invariants: Invalid calls produce specific messages; float widths allowed.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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
    // Ill-typed call: first argument must be string.
    {
        std::string src = "10 PRINT LEFT$(42,3)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("bad.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        std::ostringstream oss;
        em.printAll(oss);
        std::string out = oss.str();
        bool ok = out.find("LEFT$: arg 1 must be string") != std::string::npos;
        assert(ok);
    }
    // Wrong arity: missing required length argument.
    {
        std::string src = "10 PRINT LEFT$(\"HI\")\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("arity.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        std::ostringstream oss;
        em.printAll(oss);
        std::string out = oss.str();
        bool ok = out.find("LEFT$: expected 2 args") != std::string::npos;
        assert(ok);
    }
    // Well-typed: float width coerces to integer.
    {
        std::string src = "10 PRINT LEFT$(\"ABCD\",2.9)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ok.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        assert(em.errorCount() == 0);
    }
    return 0;
}
