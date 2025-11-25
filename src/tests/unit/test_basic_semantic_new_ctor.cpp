//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_semantic_new_ctor.cpp
// Purpose: Verify BASIC semantic analysis validates NEW expression constructor calls.
// Key invariants: Analyzer enforces constructor arity/type and accepts matching arguments.
// Ownership/Lifetime: Test owns parser, analyzer, and diagnostics per scenario.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
struct AnalysisResult
{
    size_t errors = 0;
    size_t warnings = 0;
};

AnalysisResult analyzeSource(const std::string &src)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program && "parser should succeed for test input");

    SemanticAnalyzer analyzer(emitter);
    analyzer.analyze(*program);

    return AnalysisResult{de.errorCount(), de.warningCount()};
}
} // namespace

int main()
{
    {
        const std::string src = "10 CLASS P\n"
                                "20   SUB NEW(v AS INTEGER)\n"
                                "30   END SUB\n"
                                "40 END CLASS\n"
                                "50 DIM p AS P\n"
                                "60 LET p = NEW P(5)\n"
                                "70 END\n";
        AnalysisResult result = analyzeSource(src);
        assert(result.errors == 0 && "valid constructor call should succeed");
        assert(result.warnings == 0 && "no warnings expected for valid call");
    }

    {
        const std::string src = "10 CLASS P\n"
                                "20   SUB NEW(v AS INTEGER)\n"
                                "30   END SUB\n"
                                "40 END CLASS\n"
                                "50 DIM p AS P\n"
                                "60 LET p = NEW P()\n"
                                "70 END\n";
        AnalysisResult result = analyzeSource(src);
        assert(result.errors == 1 && "constructor arity mismatch should be rejected");
    }

    {
        const std::string src = "10 CLASS P\n"
                                "20   SUB NEW(v AS INTEGER)\n"
                                "30   END SUB\n"
                                "40 END CLASS\n"
                                "50 DIM p AS P\n"
                                "60 LET p = NEW P(\"oops\")\n"
                                "70 END\n";
        AnalysisResult result = analyzeSource(src);
        assert(result.errors == 1 && "constructor argument type mismatch should error");
    }

    return 0;
}
