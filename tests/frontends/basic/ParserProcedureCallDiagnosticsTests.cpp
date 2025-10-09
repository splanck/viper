// File: tests/frontends/basic/ParserProcedureCallDiagnosticsTests.cpp
// Purpose: Verify BASIC parser reports targeted diagnostics for procedure calls missing parentheses.
// Key invariants: Known procedure identifiers without '(' emit B0001 with specific guidance.
// Ownership/Lifetime: Test owns parser and diagnostic emitter instances.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src =
        "10 SUB GREET(N$)\n"
        "20 PRINT \"Hello, \"; N$\n"
        "30 END SUB\n"
        "40 GREET \"Alice\"\n"
        "50 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("proc_call_diag.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);

    assert(emitter.errorCount() == 1);

    std::ostringstream oss;
    emitter.printAll(oss);
    const std::string output = oss.str();

    assert(output.find("error[B0001]") != std::string::npos);
    assert(output.find("expected '(' after procedure name 'GREET'") != std::string::npos);
    assert(output.find("GREET \"Alice\"") != std::string::npos);

    return 0;
}
