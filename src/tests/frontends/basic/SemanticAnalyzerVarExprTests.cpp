// File: tests/frontends/basic/SemanticAnalyzerVarExprTests.cpp
// Purpose: Validate SemanticAnalyzer variable lookup behavior for suffixed names.
// Key invariants: Variable lookups respect BASIC type suffix rules and drive
//                 diagnostics for implicit conversions.
// Ownership/Lifetime: Tests create parser/analyzer instances per snippet.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <optional>
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
    std::optional<SemanticAnalyzer::Type> symbolType;
};

AnalysisResult analyzeSnippet(const std::string &src, std::string symbol)
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

    std::optional<SemanticAnalyzer::Type> queried;
    if (!symbol.empty())
        queried = analyzer.lookupVarType(symbol);

    std::ostringstream oss;
    emitter.printAll(oss);
    return {emitter.errorCount(), emitter.warningCount(), oss.str(), queried};
}

} // namespace

int main()
{
    {
        auto result = analyzeSnippet("10 LET S! = 1\n20 LET I% = S!\n30 END\n", "S!");
        assert(result.errors == 0);
        assert(result.warnings == 1);
        assert(result.output.find("warning[B2002]") != std::string::npos);
        assert(result.symbolType.has_value());
        assert(*result.symbolType == SemanticAnalyzer::Type::Float);
    }

    {
        auto result = analyzeSnippet("10 LET F! = 1\n20 PRINT F!\n30 END\n", "F!");
        assert(result.errors == 0);
        assert(result.warnings == 0);
        assert(result.symbolType.has_value());
        assert(*result.symbolType == SemanticAnalyzer::Type::Float);
    }

    return 0;
}
