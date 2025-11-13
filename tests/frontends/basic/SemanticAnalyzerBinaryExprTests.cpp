// File: tests/frontends/basic/SemanticAnalyzerBinaryExprTests.cpp
// Purpose: Exercise BASIC semantic analyzer binary expression rules via table lookup.
// Key invariants: Each BinaryExpr::Op maps to the expected diagnostics and result handling.
// Ownership/Lifetime: Tests own parser, analyzer, and diagnostic objects locally.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"
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

std::string makeSnippet(const std::string &expr)
{
    return "10 LET X = " + expr + "\n20 END\n";
}

} // namespace

int main()
{
    {
        using Type = SemanticAnalyzer::Type;
        const auto &rule = semantic_analyzer_detail::exprRule(BinaryExpr::Op::Div);
        assert(rule.result);
        assert(rule.result(Type::Int, Type::Int) == Type::Float);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 + \"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("5 - 2"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 * TRUE"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 / \"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 / 0"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet("10 LET A = 1\n20 LET X = A / 0\n30 END\n");
        assert(result.errors == 1);
        assert(result.output.find("error[B2002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 \\ 2.5"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 \\ 0"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 MOD 2.5"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("4 MOD 0"));
        assert(result.errors == 1);
        assert(result.output.find("error[B2002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet("10 LET A = 0\n20 LET B = A\n30 LET X = 1 / B\n40 END\n");
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("\"A\" = \"B\""));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 <> \"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        // String comparison operators are now supported
        auto result = analyzeSnippet(makeSnippet("\"A\" < \"B\""));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 <= 2"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 > \"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("3 >= 1"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("TRUE ANDALSO 1"));
        assert(result.errors == 1);
        assert(result.output.find("error[E1002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("TRUE ORELSE FALSE"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("1 AND 2"));
        assert(result.errors == 1);
        assert(result.output.find("error[E1002]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("TRUE OR FALSE"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("-3"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("+4"));
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet(makeSnippet("-\"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("+\"A\""));
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet(makeSnippet("-(1.5)"));
        assert(result.errors == 0);
    }

    {
        const std::string src = "10 LET X = 3 + 1.5\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("numeric_promotion_add.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);

        SemanticAnalyzer analyzer(emitter);
        analyzer.analyze(*program);
        assert(emitter.errorCount() == 0);
        auto xType = analyzer.lookupVarType("X");
        assert(xType.has_value());
        assert(*xType == SemanticAnalyzer::Type::Float);
    }

    {
        const std::string src = "10 LET Y = 2 * (3 + 4.0)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("numeric_promotion_mul.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);

        SemanticAnalyzer analyzer(emitter);
        analyzer.analyze(*program);
        assert(emitter.errorCount() == 0);
        auto yType = analyzer.lookupVarType("Y");
        assert(yType.has_value());
        assert(*yType == SemanticAnalyzer::Type::Float);
    }

    {
        // Float type inference: A = 1.5 now infers A as Float (no warning)
        const std::string src = "10 LET A = 1.5\n20 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
        assert(result.warnings == 0);
    }

    {
        // Float type inference: Verify A is inferred as Float, not Int
        const std::string src = "10 LET A = 1.5\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("float_literal_inference.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);

        SemanticAnalyzer analyzer(emitter);
        analyzer.analyze(*program);
        auto aType = analyzer.lookupVarType("A");
        assert(aType.has_value());
        assert(*aType == SemanticAnalyzer::Type::Float);
    }

    {
        const std::string src = "10 LET X = 1 : IF X = \"A\" THEN END\n20 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        const std::string src = "10 LET S$ = \"A\" : IF S$ = \"A\" THEN END\n20 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 0);
    }

    return 0;
}
