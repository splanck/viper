// File: tests/unit/test_basic_compiler.cpp
// Purpose: Verify the BASIC compiler pipeline produces IL from in-memory input.
// Key invariants: Successful compilation yields IL functions with no diagnostics.
// Ownership/Lifetime: Test owns compiler inputs and source manager.
// Links: docs/codemap.md

#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string source = "10 PRINT 1\n20 END\n";
    SourceManager sm;
    BasicCompilerOptions options{};
    BasicCompilerInput input{source, "test.bas"};
    auto result = compileBasic(input, options, sm);

    assert(result.emitter);
    assert(result.succeeded());
    assert(result.fileId != 0);
    assert(!result.module.functions.empty());
    assert(!result.module.functions.front().name.empty());
    assert(result.emitter->warningCount() == 0);
    return 0;
}
