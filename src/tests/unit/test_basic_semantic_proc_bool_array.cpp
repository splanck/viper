// File: tests/unit/test_basic_semantic_proc_bool_array.cpp
// Purpose: Ensure semantic analyzer rejects boolean array parameters in procedures.
// Key invariants: Procedure registration forbids array parameters unless i64 or str.
// Ownership/Lifetime: Test owns constructed AST and diagnostic infrastructure.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <memory>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 SUB CHECK(B() AS BOOLEAN)\n20 END SUB\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("bool_array_param.bas");

    DiagnosticEngine engine;
    DiagnosticEmitter emitter(engine, sm);
    emitter.addSource(fid, src);

    Program prog;

    auto sub = std::make_unique<SubDecl>();
    sub->line = 10;
    sub->loc = {fid, 1, 4};
    sub->name = "CHECK";

    Param param;
    param.name = "FLAGS";
    param.type = Type::Bool;
    param.is_array = true;
    param.loc = {fid, 1, 14};
    sub->params.push_back(param);

    prog.procs.push_back(std::move(sub));

    SemanticAnalyzer sema(emitter);
    sema.analyze(prog);

    assert(emitter.errorCount() == 1);

    return 0;
}
