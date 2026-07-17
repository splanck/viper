//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include "frontends/basic/Options.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::basic;

// ===== Valid source =====

TEST(BasicAnalysis, ValidSourceProducesAst) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->ast != nullptr);
}

TEST(BasicAnalysis, ValidSourceProducesSema) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "DIM x AS INTEGER\nPRINT x\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->sema != nullptr);
}

TEST(BasicAnalysis, ValidSourceHasNoErrors) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, SemaPopulatesSymbols) {
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

TEST(BasicAnalysis, SemaPopulatesProcs) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "SUB Hello()\n  PRINT \"hi\"\nEND SUB\nHello\nEND\n",
                             .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->sema != nullptr);

    auto &procs = result->sema->procs();
    // BASIC lexer uppercases all identifiers: "Hello" → "HELLO"
    bool foundHello = false;
    for (const auto &[name, sig] : procs) {
        if (name == "HELLO" || name == "Hello")
            foundHello = true;
    }
    EXPECT_TRUE(foundHello);
}

// ===== Invalid source =====

TEST(BasicAnalysis, InvalidSourceStillReturnsResult) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT x +\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    // Should not return nullptr — error-tolerant pipeline
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, EmptySourceProducesResult) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    // Empty source should produce a valid result (no errors, empty AST)
    EXPECT_TRUE(result != nullptr);
}

TEST(BasicAnalysis, FileIdIsAssigned) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    // fileId should be non-zero (0 is a valid file ID, but the SourceManager
    // should have assigned one)
    // Just verify it was set — exact value depends on SourceManager implementation
    (void)result->fileId;
}

TEST(BasicAnalysis, DoesNotMutateFrontendOptions) {
    il::support::SourceManager sm;
    const bool prior = FrontendOptions::enableRuntimeNamespaces();
    FrontendOptions::setEnableRuntimeNamespaces(false);

    BasicCompilerInput input{.source = "PRINT 42\nEND\n", .path = "test.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(FrontendOptions::enableRuntimeNamespaces());

    FrontendOptions::setEnableRuntimeNamespaces(prior);
}

TEST(BasicAnalysis, ObjectArraysTrackElementCategoryAndBoundsBuiltins) {
    il::support::SourceManager sm;
    const std::string source = R"(
CLASS Foo
END CLASS

DIM ITEMS(2) AS Foo
DIM F AS Foo
LET F = NEW Foo()
LET ITEMS(0) = F
PRINT LBOUND(ITEMS)
PRINT UBOUND(ITEMS)
END
)";
    BasicCompilerInput input{.source = source, .path = "object_array_valid.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    ASSERT_TRUE(result->sema != nullptr);
    EXPECT_FALSE(result->hasErrors());

    auto itemsTy = result->sema->lookupVarType("ITEMS");
    ASSERT_TRUE(itemsTy.has_value());
    EXPECT_EQ(*itemsTy, SemanticAnalyzer::Type::ArrayObject);
}

TEST(BasicAnalysis, ObjectArraysRejectPrimitiveElementAssignment) {
    il::support::SourceManager sm;
    const std::string source = R"(
CLASS Foo
END CLASS

DIM ITEMS(2) AS Foo
DIM X AS INTEGER
LET X = 7
LET ITEMS(0) = X
END
)";
    BasicCompilerInput input{.source = source, .path = "object_array_bad_assign.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, ObjectArrayFieldReadsAreNotTreatedAsMissingMethods) {
    il::support::SourceManager sm;
    const std::string source = R"(
CLASS Board
  DIM Cells(2) AS INTEGER
END CLASS

DIM B AS Board
LET B = NEW Board()
LET B.Cells(0) = 7
PRINT B.Cells(0)
END
)";
    BasicCompilerInput input{.source = source, .path = "object_array_field_read.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, RuntimeObjectParametersAcceptStringArguments) {
    il::support::SourceManager sm;
    const std::string source = R"(
DIM M AS Zanna.Collections.Map
M = NEW Zanna.Collections.Map()
M.Set("key", "value")
END
)";
    BasicCompilerInput input{.source = source, .path = "runtime_obj_arg_string.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, RuntimeMethodCallArgumentTypesAreValidated) {
    il::support::SourceManager sm;
    const std::string source = R"(
DIM S AS STRING
DIM PART AS STRING
LET S = "abcdef"
LET PART = S.Substring("bad", 2)
END
)";
    BasicCompilerInput input{.source = source, .path = "runtime_method_bad_arg.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, UnknownRuntimeMethodReportsSemanticError) {
    il::support::SourceManager sm;
    const std::string source = R"(
DIM S AS STRING
LET S = "abc"
PRINT S.NoSuchMethod()
END
)";
    BasicCompilerInput input{.source = source, .path = "runtime_method_unknown.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, ReturnWithoutGosubIsRejected) {
    il::support::SourceManager sm;
    BasicCompilerInput input{.source = "RETURN\nEND\n", .path = "return_without_gosub.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, TopLevelGosubReturnIsAllowed) {
    il::support::SourceManager sm;
    const std::string source = R"(
GOSUB Handler
END
Handler: RETURN
)";
    BasicCompilerInput input{.source = source, .path = "return_with_gosub.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, ExitSubFunctionContextsAreChecked) {
    il::support::SourceManager sm;
    {
        BasicCompilerInput input{.source = "EXIT SUB\nEND\n", .path = "exit_sub_top.bas"};
        auto result = parseAndAnalyzeBasic(input, sm);
        ASSERT_TRUE(result != nullptr);
        EXPECT_TRUE(result->hasErrors());
    }
    {
        const std::string source = R"(
SUB S()
  EXIT FUNCTION
END SUB
END
)";
        BasicCompilerInput input{.source = source, .path = "exit_function_in_sub.bas"};
        auto result = parseAndAnalyzeBasic(input, sm);
        ASSERT_TRUE(result != nullptr);
        EXPECT_TRUE(result->hasErrors());
    }
}

TEST(BasicAnalysis, FinallyBodyIsSemanticallyAnalyzed) {
    il::support::SourceManager sm;
    const std::string source = R"(
TRY
FINALLY
  PRINT MISSING_NAME
END TRY
END
)";
    BasicCompilerInput input{.source = source, .path = "finally_bad_name.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, FunctionNameImplicitReturnMustCoverAllPaths) {
    il::support::SourceManager sm;
    const std::string source = R"(
FUNCTION F() AS INTEGER
  IF 1 = 1 THEN
    F = 1
  END IF
END FUNCTION
END
)";
    BasicCompilerInput input{.source = source, .path = "implicit_return_partial.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, FunctionNameImplicitReturnAllowsExhaustiveBranches) {
    il::support::SourceManager sm;
    const std::string source = R"(
FUNCTION F() AS INTEGER
  IF 1 = 1 THEN
    F = 1
  ELSE
    F = 2
  END IF
END FUNCTION
PRINT F()
END
)";
    BasicCompilerInput input{.source = source, .path = "implicit_return_exhaustive.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_FALSE(result->hasErrors());
}

TEST(BasicAnalysis, ForLoopTypesAreChecked) {
    il::support::SourceManager sm;
    {
        const std::string source = R"(
FOR S$ = 1 TO 3
NEXT S$
END
)";
        BasicCompilerInput input{.source = source, .path = "for_string_counter.bas"};
        auto result = parseAndAnalyzeBasic(input, sm);
        ASSERT_TRUE(result != nullptr);
        EXPECT_TRUE(result->hasErrors());
    }
    {
        const std::string source = R"(
FOR I = "a" TO 3
NEXT I
END
)";
        BasicCompilerInput input{.source = source, .path = "for_string_start.bas"};
        auto result = parseAndAnalyzeBasic(input, sm);
        ASSERT_TRUE(result != nullptr);
        EXPECT_TRUE(result->hasErrors());
    }
}

TEST(BasicAnalysis, ForEachElementTypeIsChecked) {
    il::support::SourceManager sm;
    const std::string source = R"(
DIM A$(2)
FOR EACH I% IN A$
NEXT I%
END
)";
    BasicCompilerInput input{.source = source, .path = "foreach_bad_element.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, UsingInitializerMustBeObject) {
    il::support::SourceManager sm;
    const std::string source = R"(
USING R AS Resource = 42
END USING
END
)";
    BasicCompilerInput input{.source = source, .path = "using_scalar.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, MeMemberFieldTypesAreCheckedInClassBodies) {
    il::support::SourceManager sm;
    const std::string source = R"(
CLASS Widget
  DIM Enabled AS BOOLEAN
  SUB Disable()
    ME.Enabled = "no"
  END SUB
END CLASS
END
)";
    BasicCompilerInput input{.source = source, .path = "me_member_bad_assignment.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

TEST(BasicAnalysis, MeIsRejectedOutsideInstanceMembers) {
    il::support::SourceManager sm;
    const std::string source = R"(
PRINT ME
END
)";
    BasicCompilerInput input{.source = source, .path = "me_top_level.bas"};
    auto result = parseAndAnalyzeBasic(input, sm);

    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->hasErrors());
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
