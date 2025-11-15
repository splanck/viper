// File: tests/unit/test_basic_diagnostic.cpp
// Purpose: Ensure DiagnosticEmitter formats BASIC diagnostics with carets and codes.
// Key invariants: Output contains error code and caret underlined range.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src = "10 PRINT X\n20 END\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();

    DiagnosticEngine de;
    DiagnosticEmitter em(de, sm);
    em.addSource(fid, src);
    SemanticAnalyzer sema(em);
    sema.analyze(*prog);

    em.emit(Severity::Error, "B9999", SourceLoc{fid, 2, 0}, 0, "zero column test");
    em.emit(Severity::Error, "B0000", SourceLoc{}, 0, "unknown location test");

    std::ostringstream oss;
    em.printAll(oss);
    std::string out = oss.str();
    assert(em.errorCount() == 3);
    assert(out.find("error[B1001]") != std::string::npos);
    assert(out.find("unknown variable 'X'") != std::string::npos);
    assert(out.find("zero column test") != std::string::npos);
    assert(out.find("test.bas:2:0") == std::string::npos);
    assert(out.find("test.bas:2: error[B9999]: zero column test") != std::string::npos);
    assert(out.find("unknown location test") != std::string::npos);
    assert(out.find("^") != std::string::npos);
    assert(out.find("\n^\n") != std::string::npos);
    const std::string unknownLocationHeader = "error[B0000]: unknown location test\n";
    auto headerPos = out.rfind(unknownLocationHeader);
    assert(headerPos != std::string::npos);
    assert(headerPos + unknownLocationHeader.size() == out.size());
    assert(out.find(": error[B0000]:") == std::string::npos);
    return 0;
}
