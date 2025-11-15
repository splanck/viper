// File: tests/frontends/basic/SemanticAnalyzerOnErrorTests.cpp
// Purpose: Validate BASIC semantic analyzer error handler tracking and RESUME diagnostics.
// Key invariants: ON ERROR establishes procedure-scoped handlers and RESUME requires active
// handlers. Ownership/Lifetime: Tests instantiate parser/analyzer per snippet; diagnostics
// collected locally. Links: docs/codemap.md

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
    uint32_t fid = sm.addFile("snippet.bas");
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
        auto result = analyzeSnippet("10 RESUME\n20 END\n");
        assert(result.errors == 1);
        assert(result.output.find("error[B1012]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet("10 ON ERROR GOTO 500\n20 END\n");
        assert(result.errors == 1);
        assert(result.output.find("error[B1003]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet("10 ON ERROR GOTO 100\n20 PRINT 1\n100 RESUME\n110 END\n");
        assert(result.errors == 0);
    }

    return 0;
}
