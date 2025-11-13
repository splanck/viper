// File: tests/frontends/basic/SemanticAnalyzerCaseInsensitiveTests.cpp
// Purpose: Ensure case-insensitive canonicalization for namespaces and proc names.
// Key invariants: Resolver accepts mixed-case qualified names and strips suffix.
// Ownership/Lifetime: Local parser/analyzer with in-memory source manager.

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/passes/CollectProcs.hpp"
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

    // Assign canonical qualified names to nested procedures prior to analysis.
    CollectProcedures(*program);
    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    std::ostringstream oss;
    emitter.printAll(oss);
    return {emitter.errorCount(), emitter.warningCount(), oss.str()};
}

} // namespace

int main()
{
    // Decl uppercase, call lowercase, qualified
    {
        const std::string src =
            "10 NAMESPACE A.B\n"
            "20   FUNCTION F$()\n"
            "30   END FUNCTION\n"
            "40 END NAMESPACE\n"
            "50 LET S$ = a.b.f()\n"
            "60 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
    }

    // Duplicate across case variants should error once
    {
        const std::string src =
            "10 NAMESPACE A.B\n"
            "20   SUB F()\n"
            "30   END SUB\n"
            "40 END NAMESPACE\n"
            "50 NAMESPACE a.b\n"
            "60   SUB f()\n"
            "70   END SUB\n"
            "80 END NAMESPACE\n"
            "90 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors >= 1);
        assert(result.output.find("duplicate procedure 'a.b.f'") != std::string::npos);
    }

    return 0;
}
