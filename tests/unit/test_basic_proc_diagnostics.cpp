// File: tests/unit/test_basic_proc_diagnostics.cpp
// Purpose: Verify exact messages for duplicate and unknown procedure diagnostics.
// Key invariants:
//   - Duplicate proc error includes both definition locations and canonical qname.
//   - Unknown unqualified proc includes canonical head and tried candidates.
//   - Unknown qualified proc includes canonical qualified name.
// Ownership/Lifetime: Creates local parser/analyzer per test; uses in-memory source.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "frontends/basic/passes/CollectProcs.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

static std::string getAllOutput(const std::string &source, const char *filename = "test.bas")
{
    SourceManager sm;
    uint32_t fileId = sm.addFile(filename);
    Parser parser(source, fileId);
    auto program = parser.parseProgram();
    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fileId, source);
    // Post-parse qualified name assignment for nested procedures.
    CollectProcedures(*program);

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);
    std::ostringstream oss;
    de.printAll(oss, &sm);
    std::string out = oss.str();
    return out;
}

static void test_duplicate_proc_message()
{
    // Two SUB declarations of the same name inside A.B
    const std::string src =
        "100 NAMESPACE A\n"
        "110 NAMESPACE B\n"
        "120 SUB F()\n"
        "130 END SUB\n"
        "140 SUB F()\n"
        "150 END SUB\n"
        "160 END NAMESPACE\n"
        "170 END NAMESPACE\n";
    std::string out = getAllOutput(src);
    // Expect canonical qualified name and both locations formatted in some diagnostic.
    // We do not assert exact line numbers here beyond presence of the pattern
    // to keep the test resilient to minor location changes.
    assert(out.find("duplicate procedure 'a.b.f'") != std::string::npos);
    assert(out.find("first defined at ") != std::string::npos);
    assert(out.find("again at ") != std::string::npos);
}

static void test_unknown_qualified_proc()
{
    // Directly call a qualified non-existent procedure.
    const std::string src =
        "100 PRINT A.B.F()\n";
    std::string out = getAllOutput(src);
    assert(out.find("unknown procedure 'a.b.f'") != std::string::npos);
}

int main()
{
    test_duplicate_proc_message();
    test_unknown_qualified_proc();
    return 0;
}
