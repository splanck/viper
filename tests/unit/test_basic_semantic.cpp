// File: tests/unit/test_basic_semantic.cpp
// Purpose: Unit test verifying BASIC semantic analyzer runs without diagnostics.
// Key invariants: Analyzer collects symbols and labels without emitting messages.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src = "10 LET X = 1\n20 END\n";
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
    assert(sema.symbols().count("X") == 1);
    assert(sema.labels().count(10) == 1);
    assert(sema.labels().count(20) == 1);
    assert(sema.labelRefs().empty());
    return 0;
}
