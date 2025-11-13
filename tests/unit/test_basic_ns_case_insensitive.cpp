// File: tests/unit/test_basic_ns_case_insensitive.cpp
// Purpose: Verify ProcRegistry canonicalizes qualified names case-insensitively and strips suffixes.
// Key invariants: Duplicate detection and lookup operate on lowercase dotted keys.
// Ownership/Lifetime: Local diagnostics/emitter/registry.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ProcRegistry.hpp"
#include "frontends/basic/SemanticDiagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    DiagnosticEngine de;
    SourceManager sm;
    DiagnosticEmitter emitter(de, sm);
    SemanticDiagnostics sdiag(emitter);
    ProcRegistry reg(sdiag);

    // Register a qualified FUNCTION with mixed case segments and suffix.
    FunctionDecl fn;
    fn.name = "F$"; // suffix should be ignored for canonical key
    fn.qualifiedName = "NameSpace.SubSpace.F$"; // simulate CollectProcedures result
    fn.ret = Type::Str;
    reg.registerProc(fn);

    // Case-insensitive duplicate: lowercased segments, no suffix
    FunctionDecl dup;
    dup.name = "f";
    dup.qualifiedName = "namespace.subspace.f";
    dup.ret = Type::Str;
    auto prevErrors = sdiag.errorCount();
    reg.registerProc(dup);
    assert(sdiag.errorCount() == prevErrors + 1); // duplicate procedure error

    // Lookup must succeed regardless of case
    const ProcSignature *sig = reg.lookup("namespace.subspace.f");
    assert(sig != nullptr);
    const ProcSignature *sig2 = reg.lookup("NameSpace.SubSpace.F");
    assert(sig2 != nullptr);

    return 0;
}

