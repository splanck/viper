// File: tests/unit/test_basic_semantic_components.cpp
// Purpose: Unit tests for scope tracking, procedure registration, and diagnostics.
// Key invariants: Components operate independently and report expected state.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ProcRegistry.hpp"
#include "frontends/basic/ScopeTracker.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include "support/source_manager.hpp"
#include <cassert>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // ScopeTracker behaviour
    ScopeTracker st;
    st.pushScope();
    st.bind("A", "A");
    auto unique = st.declareLocal("B");
    assert(st.resolve("A") && *st.resolve("A") == "A");
    assert(st.resolve("B") && *st.resolve("B") == unique);
    st.popScope();
    assert(!st.resolve("A"));

    // Diagnostics forwarding
    DiagnosticEngine eng;
    SourceManager sm;
    DiagnosticEmitter emitter(eng, sm);
    SemanticDiagnostics diag(emitter);
    diag.emit(Severity::Warning, "W0001", {}, 0, "warn");
    assert(diag.warningCount() == 1);

    // Procedure registration
    ProcRegistry reg(diag);
    FunctionDecl f;
    f.name = "FOO";
    Param p;
    p.name = "X";
    p.type = Type::I64;
    f.params.push_back(p);
    reg.registerProc(f);
    assert(reg.procs().count("FOO") == 1);
    reg.registerProc(f); // duplicate
    assert(diag.errorCount() == 1);

    return 0;
}
