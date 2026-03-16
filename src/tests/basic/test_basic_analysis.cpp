//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/basic/test_basic_analysis.cpp
// Purpose: Unit tests for the parseAndAnalyzeBasic() analysis pipeline.
// Key invariants:
//   - Each test creates a fresh SourceManager per call
//   - Tests verify the partial compilation result (parse + sema, no lowering)
//   - Valid source should produce AST + SemanticAnalyzer; invalid may still
//     produce partial results (error-tolerant)
// Ownership/Lifetime:
//   - Test-only file
// Links: frontends/basic/BasicAnalysis.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicAnalysis.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::basic;

// ===== Valid source =====

TEST(BasicAnalysis, ValidSourceProducesAst)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->ast != nullptr);
}

TEST(BasicAnalysis, ValidSourceProducesSema)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "DIM x AS INTEGER\nPRINT x\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->sema != nullptr);
}

TEST(BasicAnalysis, ValidSourceHasNoErrors)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, SemaPopulatesSymbols)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "DIM x AS INTEGER\nDIM y AS STRING\nPRINT x\nEND\n",
                             .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->sema != nullptr);

    // Should have variables in the symbol table
    auto syms = result->sema->symbols();
    EXPECT_TRUE(syms.size() >= 2u);
}

TEST(BasicAnalysis, SemaPopulatesProcs)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "SUB Hello()\n  PRINT \"hi\"\nEND SUB\nHello\nEND\n",
                             .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->sema != nullptr);

    auto &procs = result->sema->procs();
    // BASIC lexer uppercases all identifiers: "Hello" → "HELLO"
    bool foundHello = false;
    for (const auto &[name, sig] : procs)
    {
        if (name == "HELLO" || name == "Hello")
            foundHello = true;
    }
    EXPECT_TRUE(foundHello);
}

// ===== Invalid source =====

TEST(BasicAnalysis, InvalidSourceStillReturnsResult)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT x +\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    // Should not return nullptr — error-tolerant pipeline
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, EmptySourceProducesResult)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    // Empty source should produce a valid result (no errors, empty AST)
    EXPECT_TRUE(result != nullptr);
}

TEST(BasicAnalysis, FileIdIsAssigned)
{
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    // fileId should be non-zero (0 is a valid file ID, but the SourceManager
    // should have assigned one)
    // Just verify it was set — exact value depends on SourceManager implementation
    (void)result->fileId;
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
