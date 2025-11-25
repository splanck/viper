//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/SemanticsFileIoTests.cpp
// Purpose: Validate BASIC semantic analyzer file I/O statement checking.
// Key invariants: OPEN requires string paths and integer channels; CLOSE requires integer channels.
// Ownership/Lifetime: Tests allocate analyzer resources per snippet.
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
        auto result = analyzeSnippet("10 OPEN \"x\" FOR INPUT AS #1\n20 END\n");
        assert(result.errors == 0);
    }

    {
        auto result = analyzeSnippet("10 OPEN 5 FOR INPUT AS #1\n20 END\n");
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    {
        auto result = analyzeSnippet("10 CLOSE #\"1\"\n20 END\n");
        assert(result.errors == 1);
        assert(result.output.find("error[B2001]") != std::string::npos);
    }

    return 0;
}
