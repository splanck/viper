//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/SemanticAnalyzerCallStatementTests.cpp
// Purpose: Validate semantic analysis of CALL statements for SUB vs FUNCTION targets.
// Key invariants: Statement calls must target SUB procedures; function calls are rejected.
// Ownership/Lifetime: Tests allocate parser/analyzer per snippet; diagnostics captured locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
struct AnalysisResult
{
    size_t errors;
    size_t warnings;
    std::string output;
};

AnalysisResult analyzeSnippet(const std::string &src)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("stmtcall.bas");

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    std::ostringstream oss;
    emitter.printAll(oss);
    return {emitter.errorCount(), emitter.warningCount(), oss.str()};
}
} // namespace

int main()
{
    {
        const std::string src = "10 SUB GREET(N$)\n"
                                "20 PRINT \"Hi, \"; N$\n"
                                "30 END SUB\n"
                                "40 GREET(\"Alice\")\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
        assert(result.warnings == 0);
    }

    {
        const std::string src = "10 FUNCTION VALUE()\n"
                                "20 RETURN 1\n"
                                "30 END FUNCTION\n"
                                "40 VALUE()\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("error[B2015]") != std::string::npos);
        assert(result.output.find("cannot be called as a statement") != std::string::npos);
    }

    return 0;
}
