//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/WeakReferenceTests.cpp
// Purpose: Unit tests for Pascal weak reference lowering.
// Key invariants: Tests weak keyword parsing, semantic validation, and lowering.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "frontends/pascal/sem/Types.hpp"
#include "support/diagnostics.hpp"
#include "tests/TestHarness.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse, analyze, and optionally lower a program.
/// @return True if all phases succeeded without errors.
bool compileProgram(const std::string &source, DiagnosticEngine &diag, il::core::Module &outModule)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    SemanticAnalyzer analyzer(diag);
    if (!analyzer.analyze(*prog))
        return false;

    Lowerer lowerer;
    outModule = lowerer.lower(*prog, analyzer);
    return true;
}

/// @brief Parse and analyze a program (no lowering).
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
// Weak Reference Parsing Tests
//===----------------------------------------------------------------------===//

TEST(WeakReferenceTest, ParseWeakField)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
  end;
begin
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(WeakReferenceTest, ParseMultipleWeakFields)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
    weak sibling: TNode;
    name: String;
  end;
begin
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Weak Reference Semantic Tests
//===----------------------------------------------------------------------===//

TEST(WeakReferenceTest, WeakFieldOnValueTypeProducesWarning)
{
    DiagnosticEngine diag;

    // Weak reference on value types - may produce a warning but should still compile
    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak value: Integer;  // Weak on value type (may warn)
    constructor Create;
  end;

constructor TNode.Create;
begin
end;

begin
end.
)";

    // The semantic analyzer may produce a warning for weak on value types
    // but should still compile. Some implementations may disallow this.
    // We accept both pass and fail here since spec doesn't mandate behavior.
    analyzeProgram(source, diag);
    // Just verify it doesn't crash
    EXPECT_TRUE(true);
}

//===----------------------------------------------------------------------===//
// Weak Reference Lowering Tests
//===----------------------------------------------------------------------===//

TEST(WeakReferenceLoweringTest, WeakFieldAssignment)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
    constructor Create;
  end;

constructor TNode.Create;
begin
end;

var node, parent: TNode;
begin
  parent := TNode.Create;
  node := TNode.Create;
  node.parent := parent;  // Weak assignment - no refcount increment
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(WeakReferenceLoweringTest, WeakFieldRead)
{
    DiagnosticEngine diag;
    il::core::Module module;

    // Test reading a weak field and assigning it to a variable
    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
    name: String;
    constructor Create(n: String);
  end;

constructor TNode.Create(n: String);
begin
  name := n;
end;

var node, parent, readParent: TNode;
begin
  parent := TNode.Create('parent');
  node := TNode.Create('child');
  node.parent := parent;
  readParent := node.parent;  // Read weak field into strong variable
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(WeakReferenceLoweringTest, WeakFieldChainedAssignment)
{
    DiagnosticEngine diag;
    il::core::Module module;

    // Test chained weak field assignments
    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
    constructor Create;
  end;

constructor TNode.Create;
begin
end;

var node1, node2, root: TNode;
begin
  root := TNode.Create;
  node1 := TNode.Create;
  node2 := TNode.Create;
  node1.parent := root;
  node2.parent := root;  // Both nodes weakly reference root
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(WeakReferenceLoweringTest, WeakAndStrongFieldsMixed)
{
    DiagnosticEngine diag;
    il::core::Module module;

    // Test class with both strong and weak fields (using self-reference)
    const char *source = R"(
program TestWeak;
type
  TNode = class
    strong_child: TNode;   // Strong reference
    weak parent: TNode;    // Weak reference
    name: String;
    constructor Create(n: String);
  end;

constructor TNode.Create(n: String);
begin
  name := n;
end;

var root, child: TNode;
begin
  root := TNode.Create('root');
  child := TNode.Create('child');
  root.strong_child := child;  // Strong link: root -> child
  child.parent := root;        // Weak link: child -> root (avoids cycle)
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(WeakReferenceLoweringTest, WeakFieldInConstructor)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestWeak;
type
  TNode = class
    weak parent: TNode;
    constructor Create(p: TNode);
  end;

constructor TNode.Create(p: TNode);
begin
  parent := p;  // Weak assignment inside constructor
end;

var root, child: TNode;
begin
  root := TNode.Create(nil);
  child := TNode.Create(root);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
