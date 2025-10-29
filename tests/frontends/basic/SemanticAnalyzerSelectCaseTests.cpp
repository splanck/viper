// File: tests/frontends/basic/SemanticAnalyzerSelectCaseTests.cpp
// Purpose: Validate semantic analysis rules for SELECT CASE statements.
// Key invariants: SELECT CASE requires integer-compatible selectors, unique
//                 32-bit labels, and at most one CASE ELSE clause.
// Ownership/Lifetime: Each test instantiates parser/analyzer per snippet;
//                     diagnostics captured via local engine.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

#include "support/source_manager.hpp"

#include <cassert>
#include <functional>
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

AnalysisResult analyzeSnippet(const std::string &src,
                              const std::function<void(Program &)> &mutator = {})
{
    SourceManager sm;
    uint32_t fid = sm.addFile("select_case.bas");

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    if (mutator)
        mutator(*program);

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
        const std::string src = "10 SELECT CASE \"foo\"\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n"
                                "40 END SELECT\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        const std::string expected =
            "select_case.bas:2:4: error[ERR_SelectCase_StringSelectorLabels]: SELECT CASE on a "
            "string selector requires string litera"
            "l CASE labels\n"
            "20 CASE 1\n"
            "   ^\n";
        assert(result.output == expected);
    }

    {
        const std::string src = "10 SELECT CASE 0\n"
                                "20 CASE \"foo\"\n"
                                "30 PRINT 1\n"
                                "40 END SELECT\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        const std::string expected =
            "select_case.bas:2:4: error[ERR_SelectCase_StringLabelSelector]: String CASE labels "
            "require a string SELECT CASE selector\n"
            "20 CASE \"foo\"\n"
            "   ^\n";
        assert(result.output == expected);
    }

    {
        const std::string src = "10 SELECT CASE 0\n"
                                "20 CASE 1, \"foo\"\n"
                                "30 PRINT 1\n"
                                "40 END SELECT\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 2);
        const std::string expected =
            "select_case.bas:2:4: error[ERR_SelectCase_MixedLabelTypes]: mixed-type SELECT CASE\n"
            "20 CASE 1, \"foo\"\n"
            "   ^\n"
            "select_case.bas:2:4: error[ERR_SelectCase_StringLabelSelector]: String CASE labels "
            "require a string SELECT CASE selector\n"
            "20 CASE 1, \"foo\"\n"
            "   ^\n";
        assert(result.output == expected);
    }

    {
        const std::string src = "10 SELECT CASE 0\n"
                                "20 CASE 2147483648\n"
                                "30 PRINT 1\n"
                                "40 END SELECT\n"
                                "50 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        assert(result.output.find("error[B2012]") != std::string::npos);
        assert(result.output.find("outside 32-bit signed range") != std::string::npos);
    }

    {
        const std::string src = "10 SELECT CASE 0\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n"
                                "40 CASE 1\n"
                                "50 PRINT 2\n"
                                "60 END SELECT\n"
                                "70 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        const std::string expected =
            "select_case.bas:4:4: error[ERR_SelectCase_DuplicateLabel]: Duplicate CASE label: 1\n"
            "40 CASE 1\n"
            "   ^\n";
        assert(result.output == expected);
    }

    {
        const std::string src = "10 LET X = 0\n"
                                "20 SELECT CASE X\n"
                                "30 CASE 1\n"
                                "40 PRINT \"a\"\n"
                                "50 CASE 1\n"
                                "60 PRINT \"b\"\n"
                                "70 END SELECT\n"
                                "80 END\n";
        auto result = analyzeSnippet(src);
        assert(result.errors == 1);
        const std::string expected =
            "select_case.bas:5:4: error[ERR_SelectCase_DuplicateLabel]: Duplicate CASE label: 1\n"
            "50 CASE 1\n"
            "   ^\n";
        assert(result.output == expected);
    }

    {
        const std::string src = "10 SELECT CASE 0\n"
                                "20 CASE 0\n"
                                "30 PRINT 1\n"
                                "40 CASE ELSE\n"
                                "50 PRINT 2\n"
                                "60 END SELECT\n"
                                "70 END\n";

        auto result = analyzeSnippet(src,
                                     [](Program &program)
                                     {
                                         assert(!program.main.empty());
                                         auto *select = dynamic_cast<SelectCaseStmt *>(
                                             program.main.front().get());
                                         assert(select);

                                         CaseArm duplicateElse;
                                         duplicateElse.range.begin = select->range.begin;
                                         duplicateElse.range.end = select->range.begin;
                                         select->arms.push_back(std::move(duplicateElse));
                                     });

        assert(result.errors == 1);
        const std::string expected =
            "select_case.bas:1:4: error[ERR_SelectCase_DuplicateElse]: duplicate CASE ELSE\n"
            "10 SELECT CASE 0\n"
            "   ^\n";
        assert(result.output == expected);
    }

    return 0;
}
