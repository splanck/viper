// File: tests/unit/test_basic_semantic_components.cpp
// Purpose: Unit tests for scope tracking, procedure registration, and diagnostics.
// Key invariants: Components operate independently and report expected state.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/codemap.md

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

    // Duplicate parameter diagnostics for FUNCTION declarations
    FunctionDecl dupFunc;
    dupFunc.name = "BAR";
    Param dupParam;
    dupParam.name = "X";
    dupParam.type = Type::I64;
    dupFunc.params.push_back(dupParam);
    Param dupParam2 = dupParam;
    dupFunc.params.push_back(dupParam2);
    auto prevErrors = diag.errorCount();
    reg.registerProc(dupFunc);
    assert(diag.errorCount() == prevErrors + 1);

    // Duplicate parameter diagnostics for SUB declarations
    SubDecl dupSub;
    dupSub.name = "BAZ";
    Param subParam;
    subParam.name = "Y";
    subParam.type = Type::I64;
    dupSub.params.push_back(subParam);
    Param subParam2 = subParam;
    dupSub.params.push_back(subParam2);
    prevErrors = diag.errorCount();
    reg.registerProc(dupSub);
    assert(diag.errorCount() == prevErrors + 1);

    // Invalid array parameter diagnostics for FUNCTION declarations
    FunctionDecl arrayFunc;
    arrayFunc.name = "ARRFN";
    Param arrParam;
    arrParam.name = "ARR";
    arrParam.type = Type::F64;
    arrParam.is_array = true;
    arrayFunc.params.push_back(arrParam);
    prevErrors = diag.errorCount();
    reg.registerProc(arrayFunc);
    assert(diag.errorCount() == prevErrors + 1);

    // Invalid array parameter diagnostics for SUB declarations
    SubDecl arraySub;
    arraySub.name = "ARRSUB";
    Param arrSubParam;
    arrSubParam.name = "ARRS";
    arrSubParam.type = Type::F64;
    arrSubParam.is_array = true;
    arraySub.params.push_back(arrSubParam);
    prevErrors = diag.errorCount();
    reg.registerProc(arraySub);
    assert(diag.errorCount() == prevErrors + 1);

    return 0;
}
