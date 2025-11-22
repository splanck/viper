//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_function_as_type.cpp
// Purpose: Ensure BASIC functions with explicit return types lower to IL with correct signatures. 
// Key invariants: Parser accepts FUNCTION ... AS syntax and lowerer records string/double return
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <cstdint>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("t.bas");
    // String function case:
    {
        std::string src = "FUNCTION EXCL$(S$) AS STRING\n"
                          "  RETURN S$+\"!\"\n"
                          "END FUNCTION\n";
        Parser p(src, fid);
        auto prog = p.parseProgram();
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sa(em);
        sa.analyze(*prog);
        Lowerer lw(/*boundsChecks*/ false);
        auto mod = lw.lower(*prog);
        bool saw = false;
        for (auto &fn : mod.functions)
        {
            if (fn.name == "EXCL" || fn.name == "EXCL$")
            {
                assert(fn.retType.kind == il::core::Type::Kind::Str);
                saw = true;
            }
        }
        assert(saw);
    }
    // Float function case:
    {
        std::string src = "FUNCTION F(X) AS DOUBLE\n"
                          "  RETURN X*2.5\n"
                          "END FUNCTION\n";
        Parser p(src, fid);
        auto prog = p.parseProgram();
        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sa(em);
        sa.analyze(*prog);
        Lowerer lw(false);
        auto mod = lw.lower(*prog);
        bool saw = false;
        for (auto &fn : mod.functions)
        {
            if (fn.name == "F")
            {
                assert(fn.retType.kind == il::core::Type::Kind::F64);
                saw = true;
            }
        }
        assert(saw);
    }
    return 0;
}
