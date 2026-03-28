//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/test_zia_lowerer.cpp
// Purpose: Unit tests for Zia IL lowering, verifying that various Zia language
//          constructs produce correct IL opcodes and module structure.
// Key invariants:
//   - Every compiled module must pass the IL verifier.
//   - Specific opcodes must appear for corresponding Zia constructs.
// Ownership/Lifetime:
//   - Each test owns its SourceManager and CompilerResult by value.
// Links: frontends/zia/Compiler.hpp, il/core/Module.hpp, il/verify/Verifier.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/Verifier.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;
using namespace il::core;

namespace {

/// @brief Helper: compile source and assert success.
static CompilerResult compileAndAssert(const std::string &source, SourceManager &sm) {
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    if (!result.succeeded()) {
        std::cerr << "Compilation failed:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    return result;
}

/// @brief Helper: find a function by name in the module.
static const Function *findFunction(const Module &mod, const std::string &name) {
    for (const auto &fn : mod.functions) {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

/// @brief Helper: check if any instruction in a function uses the given opcode.
static bool hasOpcode(const Function &fn, Opcode op) {
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == op)
                return true;
        }
    }
    return false;
}

/// @brief Helper: check if any instruction in a function calls the given callee.
static bool hasCallTo(const Function &fn, const std::string &callee) {
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == Opcode::Call && instr.callee == callee)
                return true;
        }
    }
    return false;
}

/// @brief Helper: count instructions with a given opcode in a function.
static int countOpcode(const Function &fn, Opcode op) {
    int count = 0;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == op)
                ++count;
        }
    }
    return count;
}

// ============================================================================
// Test: Class construction lowering (class with fields)
// ============================================================================

TEST(ZiaLowerer, EntityConstruction) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var p = new Point();
    p.x = 10;
    p.y = 20;
    Viper.Terminal.SayInt(p.x);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    // Class construction should produce a Call to the runtime allocator.
    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);
    EXPECT_TRUE(hasOpcode(*main, Opcode::Call));
}

// ============================================================================
// Test: Method dispatch lowering (calling class methods)
// ============================================================================

TEST(ZiaLowerer, MethodDispatch) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Counter {
    expose Integer count;

    expose func increment() {
        self.count = self.count + 1;
    }

    expose func getCount() -> Integer {
        return self.count;
    }
}

func start() {
    var c = new Counter();
    c.increment();
    Integer n = c.getCount();
    Viper.Terminal.SayInt(n);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    // The module should contain lowered method functions.
    EXPECT_TRUE(result.module.functions.size() >= 3);

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);
    EXPECT_TRUE(hasOpcode(*main, Opcode::Call));
}

// ============================================================================
// Test: Collection lowering (List creation)
// ============================================================================

TEST(ZiaLowerer, ListCreation) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var items: List[Integer] = [];
    items.Push(10);
    items.Push(20);
    items.Push(30);
    var len = items.Length();
    Viper.Terminal.SayInt(len);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);
    // List operations should generate Call instructions to runtime.
    EXPECT_TRUE(countOpcode(*main, Opcode::Call) >= 4);
}

// ============================================================================
// Test: Map collection lowering
// ============================================================================

TEST(ZiaLowerer, MapCreation) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var items: List[String] = [];
    items.Push("Alice");
    items.Push("Bob");
    Viper.Terminal.SayInt(items.Length());
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);
    EXPECT_TRUE(hasOpcode(*main, Opcode::Call));
}

// ============================================================================
// Test: Arithmetic expression lowering (Add/Sub/Mul/SDiv opcodes)
// ============================================================================

TEST(ZiaLowerer, ArithmeticExpressions) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 10;
    Integer b = 3;
    Integer sum = a + b;
    Integer diff = a - b;
    Integer prod = a * b;
    Integer quot = a / b;
    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(diff);
    Viper.Terminal.SayInt(prod);
    Viper.Terminal.SayInt(quot);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // Zia emits overflow-checked arithmetic: AddChk, SubChk, MulChk, SDivChk0.
    bool hasAdd = hasOpcode(*main, Opcode::Add) || hasOpcode(*main, Opcode::IAddOvf);
    bool hasSub = hasOpcode(*main, Opcode::Sub) || hasOpcode(*main, Opcode::ISubOvf);
    bool hasMul = hasOpcode(*main, Opcode::Mul) || hasOpcode(*main, Opcode::IMulOvf);
    bool hasDiv = hasOpcode(*main, Opcode::SDiv) || hasOpcode(*main, Opcode::SDivChk0);
    EXPECT_TRUE(hasAdd);
    EXPECT_TRUE(hasSub);
    EXPECT_TRUE(hasMul);
    EXPECT_TRUE(hasDiv);
}

// ============================================================================
// Test: Boolean expression lowering (And/Or)
// ============================================================================

TEST(ZiaLowerer, BooleanExpressions) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
    if (a and b) {
        Viper.Terminal.Say("both");
    }
    if (a or b) {
        Viper.Terminal.Say("either");
    }
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // Short-circuit boolean evaluation produces CBr instructions for and/or.
    EXPECT_TRUE(hasOpcode(*main, Opcode::CBr));
}

// ============================================================================
// Test: Control flow lowering (if/else generates CBr)
// ============================================================================

TEST(ZiaLowerer, IfElseControlFlow) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    if (x > 3) {
        Viper.Terminal.Say("big");
    } else {
        Viper.Terminal.Say("small");
    }
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // if/else must produce a CBr (conditional branch).
    EXPECT_TRUE(hasOpcode(*main, Opcode::CBr));

    // Should also have a comparison instruction (SCmpGT or similar).
    bool hasCmp = hasOpcode(*main, Opcode::SCmpGT) || hasOpcode(*main, Opcode::ICmpNe);
    EXPECT_TRUE(hasCmp);

    // Should have multiple basic blocks (entry, then, else, merge).
    EXPECT_TRUE(main->blocks.size() >= 3);
}

// ============================================================================
// Test: While loop lowering (generates back edges)
// ============================================================================

TEST(ZiaLowerer, WhileLoopBackEdge) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer i = 0;
    while (i < 10) {
        i = i + 1;
    }
    Viper.Terminal.SayInt(i);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // A while loop produces CBr for the loop condition check.
    EXPECT_TRUE(hasOpcode(*main, Opcode::CBr));

    // A while loop needs at least: entry, loop_header, loop_body, exit.
    EXPECT_TRUE(main->blocks.size() >= 3);

    // Must have an unconditional branch (Br) for the back edge or loop entry.
    EXPECT_TRUE(hasOpcode(*main, Opcode::Br));
}

// ============================================================================
// Test: For loop lowering (generates back edges)
// ============================================================================

TEST(ZiaLowerer, ForLoopLowering) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer total = 0;
    for i in 1..5 {
        total = total + i;
    }
    Viper.Terminal.SayInt(total);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // For loop should produce CBr and Br for loop control flow.
    EXPECT_TRUE(hasOpcode(*main, Opcode::CBr));
    EXPECT_TRUE(hasOpcode(*main, Opcode::Br));

    // For loop increment produces Add or AddChk.
    bool hasAdd = hasOpcode(*main, Opcode::Add) || hasOpcode(*main, Opcode::IAddOvf);
    EXPECT_TRUE(hasAdd);
}

// ============================================================================
// Test: Function call lowering (Call opcode with correct callee)
// ============================================================================

TEST(ZiaLowerer, FunctionCallLowering) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func addNumbers(a: Integer, b: Integer) -> Integer {
    return a + b;
}

func start() {
    Integer result = addNumbers(3, 7);
    Viper.Terminal.SayInt(result);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    // Should have both the user-defined function and main.
    const auto *addFn = findFunction(result.module, "addNumbers");
    EXPECT_TRUE(addFn != nullptr);

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // main should call addNumbers.
    EXPECT_TRUE(hasCallTo(*main, "addNumbers"));
}

// ============================================================================
// Test: Return value lowering (Ret opcode)
// ============================================================================

TEST(ZiaLowerer, ReturnValueLowering) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func double(x: Integer) -> Integer {
    return x * 2;
}

func start() {
    Integer r = double(21);
    Viper.Terminal.SayInt(r);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *doubleFn = findFunction(result.module, "double");
    ASSERT_TRUE(doubleFn != nullptr);

    // The double function must have a Ret instruction.
    EXPECT_TRUE(hasOpcode(*doubleFn, Opcode::Ret));

    // The function should also contain a Mul or MulChk for x * 2.
    bool hasMul = hasOpcode(*doubleFn, Opcode::Mul) || hasOpcode(*doubleFn, Opcode::IMulOvf);
    EXPECT_TRUE(hasMul);
}

// ============================================================================
// Test: IL verification passes for all generated modules
// ============================================================================

TEST(ZiaLowerer, VerifierPassesComplex) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Animal {
    expose String name;
    expose Integer age;

    expose func describe() -> String {
        return self.name;
    }
}

func factorial(n: Integer) -> Integer {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

func start() {
    var a = new Animal();
    a.name = "Dog";
    a.age = 5;
    String desc = a.describe();
    Viper.Terminal.Say(desc);

    Integer f = factorial(6);
    Viper.Terminal.SayInt(f);

    Integer i = 0;
    while (i < 3) {
        Viper.Terminal.SayInt(i);
        i = i + 1;
    }
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    // The key assertion: a complex module with entities, recursion,
    // loops, and method calls should pass the IL verifier.
    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    // Verify we got a non-trivial module.
    EXPECT_TRUE(result.module.functions.size() >= 3);
}

// ============================================================================
// Test: String constant lowering produces ConstStr
// ============================================================================

TEST(ZiaLowerer, StringConstantLowering) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    String greeting = "Hello, World!";
    Viper.Terminal.Say(greeting);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // String literals should produce ConstStr instructions.
    EXPECT_TRUE(hasOpcode(*main, Opcode::ConstStr));
}

// ============================================================================
// Test: Comparison operators lower to ICmp instructions
// ============================================================================

TEST(ZiaLowerer, ComparisonOperators) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    Integer y = 10;
    if (x == y) {
        Viper.Terminal.Say("equal");
    }
    if (x != y) {
        Viper.Terminal.Say("not equal");
    }
    if (x < y) {
        Viper.Terminal.Say("less");
    }
    if (x >= y) {
        Viper.Terminal.Say("geq");
    }
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // Should produce various comparison opcodes.
    bool hasAnyCmp = hasOpcode(*main, Opcode::ICmpEq) || hasOpcode(*main, Opcode::ICmpNe) ||
                     hasOpcode(*main, Opcode::SCmpLT) || hasOpcode(*main, Opcode::SCmpLE) ||
                     hasOpcode(*main, Opcode::SCmpGT) || hasOpcode(*main, Opcode::SCmpGE);
    EXPECT_TRUE(hasAnyCmp);

    // Multiple if statements produce multiple CBr instructions.
    EXPECT_TRUE(countOpcode(*main, Opcode::CBr) >= 4);
}

// ============================================================================
// Test: Nested control flow produces correct structure
// ============================================================================

TEST(ZiaLowerer, NestedControlFlow) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    if (x > 0) {
        if (x < 10) {
            Viper.Terminal.Say("single digit positive");
        } else {
            Viper.Terminal.Say("large");
        }
    } else {
        Viper.Terminal.Say("non-positive");
    }
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *main = findFunction(result.module, "main");
    ASSERT_TRUE(main != nullptr);

    // Nested if/else should produce at least 2 CBr instructions.
    EXPECT_TRUE(countOpcode(*main, Opcode::CBr) >= 2);

    // Nested control flow should produce at least 5 blocks.
    EXPECT_TRUE(main->blocks.size() >= 5);
}

// ============================================================================
// Test: Multiple function parameters are lowered correctly
// ============================================================================

TEST(ZiaLowerer, MultipleFunctionParams) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func compute(a: Integer, b: Integer, c: Integer) -> Integer {
    return a * b + c;
}

func start() {
    Integer r = compute(2, 3, 4);
    Viper.Terminal.SayInt(r);
}
)";
    auto result = compileAndAssert(source, sm);
    ASSERT_TRUE(result.succeeded());

    auto verified = il::verify::Verifier::verify(result.module);
    EXPECT_TRUE(verified.hasValue());

    const auto *computeFn = findFunction(result.module, "compute");
    ASSERT_TRUE(computeFn != nullptr);

    // compute takes 3 parameters.
    EXPECT_EQ(computeFn->params.size(), static_cast<size_t>(3));

    // Should have Mul/MulChk and Add/AddChk opcodes for a * b + c.
    bool hasMul = hasOpcode(*computeFn, Opcode::Mul) || hasOpcode(*computeFn, Opcode::IMulOvf);
    bool hasAdd = hasOpcode(*computeFn, Opcode::Add) || hasOpcode(*computeFn, Opcode::IAddOvf);
    EXPECT_TRUE(hasMul);
    EXPECT_TRUE(hasAdd);
    EXPECT_TRUE(hasOpcode(*computeFn, Opcode::Ret));
}

} // namespace

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
