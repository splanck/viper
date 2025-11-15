// File: tests/frontends/basic/SemanticAnalyzerCallTests.cpp
// Purpose: Validate BASIC semantic analyzer argument count checking for procedure calls.
// Key invariants: Calls with mismatched argument counts emit B2008 diagnostics and halt further
//                 analysis for that invocation.
// Ownership/Lifetime: Tests construct parser/analyzer per snippet; diagnostics captured locally.
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
        const std::string src = "10 FUNCTION INC(X)\n"
                                "20 RETURN X + 1\n"
                                "30 END FUNCTION\n"
                                "40 LET Y = INC(5)\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
    }

    {
        const std::string src = "10 FUNCTION INC(X)\n"
                                "20 RETURN X + 1\n"
                                "30 END FUNCTION\n"
                                "40 LET Y = INC()\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("error[B2008]") != std::string::npos);
        assert(result.output.find("argument count mismatch for 'INC'") != std::string::npos);
    }

    return 0;
}
