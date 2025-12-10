//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticNonNullableTests.cpp
// Purpose: Unit tests for non-nullable class/interface semantics.
// Key invariants: Non-nullable locals require definite assignment before use;
//                 nil cannot be assigned to non-optional class/interface types.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../GTestStub.hpp"
#endif

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse and analyze a program.
/// @return True if analysis succeeded without errors.
bool analyzeProgram(const std::string &source, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    SemanticAnalyzer analyzer(diag);
    return analyzer.analyze(*prog);
}

//===----------------------------------------------------------------------===//
// Nil Assignment to Non-Nullable Tests
//===----------------------------------------------------------------------===//

TEST(PascalNonNullableTest, NilToNonOptionalClassRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  node := nil;\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, NilToOptionalClassAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "var node: TNode?;\n"
                                 "begin\n"
                                 "  node := nil;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, NilToNonOptionalInterfaceRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type IDoer = interface\n"
                                 "  procedure DoIt;\n"
                                 "end;\n"
                                 "type TDoer = class(IDoer)\n"
                                 "public\n"
                                 "  procedure DoIt;\n"
                                 "end;\n"
                                 "procedure TDoer.DoIt;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var doer: IDoer;\n"
                                 "begin\n"
                                 "  doer := nil;\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, NilToOptionalInterfaceAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type IDoer = interface\n"
                                 "  procedure DoIt;\n"
                                 "end;\n"
                                 "type TDoer = class(IDoer)\n"
                                 "public\n"
                                 "  procedure DoIt;\n"
                                 "end;\n"
                                 "procedure TDoer.DoIt;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var doer: IDoer?;\n"
                                 "begin\n"
                                 "  doer := nil;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Definite Assignment Tests
//===----------------------------------------------------------------------===//

TEST(PascalNonNullableTest, UninitializedNonNullableLocalRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork;\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, InitializedNonNullableLocalAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork;\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  node := TNode.Create;\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, OptionalLocalNoDefiniteAssignmentRequired)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork;\n"
                                 "var node: TNode?;\n"
                                 "begin\n"
                                 "  if node <> nil then\n"
                                 "    WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, IntegerLocalNoDefiniteAssignmentRequired)
{
    // Primitive types don't require definite assignment
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure DoWork;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  WriteLn(x);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, GlobalNonNullableNoDefiniteAssignmentCheck)
{
    // Global variables are not checked for definite assignment
    // (they would be default-initialized at program start)
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "var globalNode: TNode;\n"
                                 "begin\n"
                                 "  WriteLn(globalNode.value);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, UseAfterAssignmentAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork;\n"
                                 "var a, b: TNode;\n"
                                 "begin\n"
                                 "  a := TNode.Create;\n"
                                 "  b := TNode.Create;\n"
                                 "  WriteLn(a.value);\n"
                                 "  WriteLn(b.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, UseBeforeAssignmentInSameBlockRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork;\n"
                                 "var a, b: TNode;\n"
                                 "begin\n"
                                 "  WriteLn(a.value);\n" // a not yet assigned
                                 "  a := TNode.Create;\n"
                                 "  b := TNode.Create;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, InterfaceLocalRequiresDefiniteAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type IDoer = interface\n"
                                 "  procedure DoIt;\n"
                                 "end;\n"
                                 "procedure Work;\n"
                                 "var doer: IDoer;\n"
                                 "begin\n"
                                 "  doer.DoIt;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, ParameterNotCheckedForDefiniteAssignment)
{
    // Parameters are already initialized when passed
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(node: TNode);\n"
                                 "begin\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, SelfNotCheckedForDefiniteAssignment)
{
    // Self is always available in methods
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "  procedure DoWork;\n"
                                 "end;\n"
                                 "procedure TNode.DoWork;\n"
                                 "begin\n"
                                 "  WriteLn(Self.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Assignment From Objects
//===----------------------------------------------------------------------===//

TEST(PascalNonNullableTest, AssignFromAnotherObjectAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(other: TNode);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  node := other;\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, DynamicArrayDoesNotRequireDefiniteAssignment)
{
    // Dynamic arrays default to nil/empty and don't require definite assignment
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure DoWork;\n"
                                 "var arr: array of Integer;\n"
                                 "begin\n"
                                 "  WriteLn(Length(arr));\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Control-Flow Aware Initialization Tests
//===----------------------------------------------------------------------===//

TEST(PascalNonNullableTest, InitializedInBothBranchesAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "    node := TNode.Create\n"
                                 "  else\n"
                                 "    node := TNode.Create;\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, InitializedInOnlyThenBranchRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "    node := TNode.Create;\n"
                                 "  // no else - node may not be initialized\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, InitializedInOnlyElseBranchRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "    WriteLn('no init')\n"
                                 "  else\n"
                                 "    node := TNode.Create;\n"
                                 "  WriteLn(node.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, UseInsideThenBranchAfterAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "  begin\n"
                                 "    node := TNode.Create;\n"
                                 "    WriteLn(node.value);\n"
                                 "  end;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, UseInsideThenBranchBeforeAssignmentRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var node: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "  begin\n"
                                 "    WriteLn(node.value);\n"
                                 "    node := TNode.Create;\n"
                                 "  end;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, MultipleVarsInitializedInBothBranches)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var a, b: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "  begin\n"
                                 "    a := TNode.Create;\n"
                                 "    b := TNode.Create;\n"
                                 "  end\n"
                                 "  else\n"
                                 "  begin\n"
                                 "    a := TNode.Create;\n"
                                 "    b := TNode.Create;\n"
                                 "  end;\n"
                                 "  WriteLn(a.value);\n"
                                 "  WriteLn(b.value);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalNonNullableTest, OneVarInitializedInBothOtherInOnlyOne)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TNode = class\n"
                                 "public\n"
                                 "  value: Integer;\n"
                                 "end;\n"
                                 "procedure DoWork(flag: Boolean);\n"
                                 "var a, b: TNode;\n"
                                 "begin\n"
                                 "  if flag then\n"
                                 "  begin\n"
                                 "    a := TNode.Create;\n"
                                 "    b := TNode.Create;\n"
                                 "  end\n"
                                 "  else\n"
                                 "  begin\n"
                                 "    a := TNode.Create;\n"
                                 "    // b not initialized in else\n"
                                 "  end;\n"
                                 "  WriteLn(a.value);\n" // a is fine - init in both
                                 "  WriteLn(b.value);\n" // b is NOT - only init in then
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
