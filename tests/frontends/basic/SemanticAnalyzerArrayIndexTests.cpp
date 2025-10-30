// File: tests/frontends/basic/SemanticAnalyzerArrayIndexTests.cpp
// Purpose: Validate array index semantic checks handle literal conversions.
// Key invariants: Float literals used as indices trigger B2002 with implicit casts;
//                 incompatible literal types continue to raise B2001 errors.
// Ownership/Lifetime: Tests allocate parser/analyzer resources per snippet.
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
        const std::string src = "10 DIM A(10)\n20 PRINT A(1.5#)\n30 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
        assert(result.warnings == 1);
        assert(result.output.find("warning[B2002]") != std::string::npos);
    }

    {
        const std::string src = "10 DIM A(10)\n20 LET A(1.5#) = 1\n30 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
        assert(result.warnings == 1);
        assert(result.output.find("warning[B2002]") != std::string::npos);
    }

    {
        const std::string src = "10 DIM A(10)\n20 PRINT A(\"foo\")\n30 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    return 0;
}

