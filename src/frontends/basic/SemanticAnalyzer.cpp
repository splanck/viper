// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Lightweight orchestrator invoking BASIC semantic analysis passes.
// Key invariants: Delegates to SymbolCollector then TypeChecker.
// Ownership/Lifetime: Borrows DiagnosticEmitter; AST nodes owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/SemanticAnalyzer.hpp"

namespace il::frontends::basic
{

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEmitter &de)
    : de_(de), collector_(de), checker_(de)
{
}

void SemanticAnalyzer::analyze(const Program &prog)
{
    collector_.collect(prog);
    symbols_ = collector_.symbols();
    labels_ = collector_.labels();
    labelRefs_ = collector_.labelRefs();
    checker_.analyze(prog);
    symbols_ = checker_.symbols();
    labels_ = checker_.labels();
    labelRefs_ = checker_.labelRefs();
    procs_ = checker_.procs();
}

} // namespace il::frontends::basic

