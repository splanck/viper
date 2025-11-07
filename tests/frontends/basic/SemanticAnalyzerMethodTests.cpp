// File: tests/frontends/basic/SemanticAnalyzerMethodTests.cpp
// Purpose: Ensure semantic analyzer validates class method return contracts.
// Key invariants: RETURN expressions must match declared types and required
//                 returns are diagnosed when missing.
// Ownership/Lifetime: Test constructs parser and analyzer per snippet.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
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
        const std::string src = "10 CLASS Box\n"
                                 "20   FUNCTION Title() AS STRING\n"
                                 "30     RETURN 42\n"
                                 "40   END FUNCTION\n"
                                 "50 END CLASS\n"
                                 "60 END\n";
        auto result = analyzeSnippet(src);
        assert(result.warnings == 1);
        assert(result.output.find("B4010") != std::string::npos);
    }

    {
        const std::string src = "10 CLASS Box\n"
                                 "20   FUNCTION Value() AS INTEGER\n"
                                 "30   END FUNCTION\n"
                                 "40 END CLASS\n"
                                 "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("B1007") != std::string::npos);
    }

    return 0;
}
