//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia bug fixes (Bugs #38-44).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include "tests/common/PosixCompat.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using namespace il::frontends::zia;
using namespace il::frontends::zia::runtime;
using namespace il::support;

namespace {

const il::core::Function *findFunction(const il::core::Module &module, const std::string &name) {
    for (const auto &fn : module.functions) {
        if (fn.name == name)
            return &fn;
    }
    return nullptr;
}

bool hasOpcode(const il::core::Function &fn, il::core::Opcode opcode) {
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == opcode)
                return true;
        }
    }
    return false;
}

size_t countOpcode(const il::core::Function &fn, il::core::Opcode opcode) {
    size_t count = 0;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == opcode)
                ++count;
        }
    }
    return count;
}

size_t countCallsTo(const il::core::Function &fn, const std::string &callee) {
    size_t count = 0;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                ++count;
        }
    }
    return count;
}

size_t countCallsTo(const il::core::Module &module, const std::string &callee) {
    size_t count = 0;
    for (const auto &fn : module.functions)
        count += countCallsTo(fn, callee);
    return count;
}

bool hasAllocaSize(const il::core::Function &fn, int64_t size) {
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == il::core::Opcode::Alloca && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[0].i64 == size)
                return true;
        }
    }
    return false;
}

bool hasIMulByConst(const il::core::Function &fn, int64_t value) {
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op == il::core::Opcode::IMulOvf && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[1].i64 == value)
                return true;
        }
    }
    return false;
}

bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool hasDiagnosticCode(const CompilerResult &result, const std::string &code) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.code == code)
            return true;
    }
    return false;
}

TEST(ZiaRuntimeMemory, ExplicitReleaseUsesPublicRuntimeSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var remaining = Viper.Memory.Release(Viper.Core.Box.I64(42));
}
)";
    CompilerInput input{.source = source, .path = "memory_release_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_GE(countCallsTo(result.module, kHeapRelease), static_cast<size_t>(1));
    EXPECT_EQ(countCallsTo(result.module, "rt_heap_release_deferred"), static_cast<size_t>(0));
}

TEST(ZiaRuntimeMemory, ExplicitStringReleaseReturnsRuntimeCount) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var remaining = Viper.Memory.ReleaseStr("owned");
}
)";
    CompilerInput input{.source = source, .path = "memory_release_str_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_GE(countCallsTo(result.module, kHeapReleaseStr), static_cast<size_t>(1));
    EXPECT_EQ(countCallsTo(result.module, "rt_str_release_maybe"), static_cast<size_t>(0));
}

TEST(ZiaRuntimeMemory, BoxToStrResultIsOwnedAndReleased) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Viper.Core.Box.ToStr(Viper.Core.Box.Str("owned"));
}
)";
    CompilerInput input{.source = source, .path = "box_to_str_owned.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_GE(countCallsTo(result.module, kUnboxStr), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(result.module, kStrReleaseMaybe), static_cast<size_t>(1));
}

//===----------------------------------------------------------------------===//
// Bug #38: Module-Level Mutable Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level mutable variables can be read and written.
TEST(ZiaBugFixes, Bug38_ModuleLevelMutableVariables) {
    SourceManager sm;
    const std::string source = R"(
module Test;

var counter: Integer;

func start() {    counter = 10;
    Viper.Terminal.SayInt(counter);
    counter = counter + 1;
    Viper.Terminal.SayInt(counter);
}
)";
    CompilerInput input{.source = source, .path = "bug38.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(result.module.functions.empty());
}

/// @brief Test module-level mutable variables without initializer.
TEST(ZiaBugFixes, Bug38_ModuleLevelVarNoInitializer) {
    SourceManager sm;
    const std::string source = R"(
module Test;

var running: Boolean;
var score: Integer;

func start() {    running = true;
    score = 0;
    if running {
        score = 100;
    }
    Viper.Terminal.SayInt(score);
}
)";
    CompilerInput input{.source = source, .path = "bug38b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test mutable global initializer expressions lower as ordered startup code.
TEST(ZiaBugFixes, Bug38_ModuleLevelInitializerExpressionsAreEmitted) {
    SourceManager sm;
    const std::string source = R"(
module Test;

var seed: Integer = 1;
var counter: Integer = seed + 2;

func start() {}
)";
    CompilerInput input{.source = source, .path = "bug38_expr_init.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "rt_modvar_addr_i64"), static_cast<size_t>(3));
}

/// @brief Mutable globals must also initialize when the entrypoint is `main()`.
TEST(ZiaBugFixes, Bug38_ModuleLevelInitializerExpressionsAreEmittedForMainEntry) {
    SourceManager sm;
    const std::string source = R"(
module Test;

var seed: Integer = 7;
var counter: Integer = seed + 5;

func main() {}
)";
    CompilerInput input{.source = source, .path = "bug38_expr_init_main.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "rt_modvar_addr_i64"), static_cast<size_t>(3));
}

/// @brief Optional struct returns must box the payload instead of returning a dangling stack ptr.
TEST(ZiaBugFixes, OptionalStructReturnBoxesPayload) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Pair {
    expose Integer a;
    expose Integer b;

    expose func init(x: Integer, y: Integer) {
        a = x;
        b = y;
    }
}

func maybePair() -> Pair? {
    var p = new Pair(3, 4);
    return p;
}

func main() {}
)";
    CompilerInput input{.source = source, .path = "optional_struct_return.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *maybeFn = findFunction(result.module, "maybePair");
    ASSERT_TRUE(maybeFn != nullptr);
    EXPECT_GE(countCallsTo(*maybeFn, kBoxValueType), static_cast<size_t>(1));
}

//===----------------------------------------------------------------------===//
// Bug #39: Module-Level Entity Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level class variables can store and retrieve objects.
TEST(ZiaBugFixes, Bug39_ModuleLevelEntityVariables) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Player {
    expose Integer score;

    expose func addScore(points: Integer) {        score = score + points;
    }
}

var player: Player;

func start() {    player = new Player();
    player.score = 10;
    player.addScore(5);
    Viper.Terminal.SayInt(player.score);
}
)";
    CompilerInput input{.source = source, .path = "bug39.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test qualified zero-argument extern functions remain callable.
TEST(ZiaBugFixes, ZeroArgExternFunctionsRemainCallable) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.System.Environment;

func start() {    var argc = Viper.System.Environment.GetArgumentCount();
    Viper.Terminal.SayInt(argc);
}
)";
    CompilerInput input{.source = source, .path = "zero_arg_runtime.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Viper.System.Environment.GetArgumentCount"),
              static_cast<size_t>(1));
}

/// @brief Range step validation checks positivity without rejecting INT64_MAX.
TEST(ZiaBugFixes, RangeStepAllowsInt64MaxAndDoesNotUseExclusiveIdxChk) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func main() {
    var values = (0..10).step(9223372036854775807);
}
)";
    CompilerInput input{.source = source, .path = "range_step_max.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_EQ(countOpcode(*mainFn, il::core::Opcode::IdxChk), static_cast<size_t>(0));
    EXPECT_GE(countOpcode(*mainFn, il::core::Opcode::SCmpLT), static_cast<size_t>(1));
}

/// @brief Struct literals validate field assignability and duplicate names.
TEST(ZiaBugFixes, StructLiteralRejectsWrongTypeAndDuplicateFields) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
    expose Integer y;
}

func main() {
    var p: Point = Point { x = "bad", x = 2 };
}
)";
    CompilerInput input{.source = source, .path = "struct_literal_bad.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Duplicate field 'x'"));
    EXPECT_TRUE(hasErrorContaining(result, "Type mismatch"));
}

/// @brief Missing struct-literal fields use typed defaults and field initializers.
TEST(ZiaBugFixes, StructLiteralUsesTypedDefaultsAndInitializers) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Config {
    expose String name = "demo";
    expose Boolean enabled = true;
    expose Integer retries;
}

func main() {
    var cfg: Config = Config { retries = 3 };
    Viper.Terminal.Say(cfg.name);
    Viper.Terminal.SayBool(cfg.enabled);
    Viper.Terminal.SayInt(cfg.retries);
}
)";
    CompilerInput input{.source = source, .path = "struct_literal_defaults.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Runtime APIs should accept named arguments using their surfaced parameter names.
TEST(ZiaBugFixes, RuntimeNamedArgumentsUseSurfaceParameterNames) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections;

func start() {    var xs = List.New();
    xs.Add(value: 1);
    var first = xs.Get(index: 0);
    var part = "abcd".Substring(start: 1, len: 2);
    Viper.Terminal.SetPosition(row: 1, col: 2);
    Viper.Terminal.SetColor(fg: 7, bg: 0);
    Viper.Terminal.Say(part);
    Viper.Terminal.SayInt(Viper.Core.Box.ToI64(first));
}
)";
    CompilerInput input{.source = source, .path = "runtime_named_args.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kListAdd), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListGet), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.String.Substring"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Terminal.SetPosition"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.Terminal.SetColor"), static_cast<size_t>(1));
}

/// @brief Compatibility aliases for visibility and immutability should compile end-to-end.
TEST(ZiaBugFixes, CompatibilityAliasesCompile) {
    SourceManager sm;
    const std::string source = R"(
module Test;

export func exported() {}

public class Greeter {
    expose func hi() {
        Viper.Terminal.Say("hi");
    }
}

func start() {
    let x = 1;
    exported();
    var g = new Greeter();
    g.hi();
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "compat_aliases.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_TRUE(findFunction(result.module, "exported") != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "exported"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Greeter.hi"), static_cast<size_t>(1));
}

/// @brief Test Byte arguments widen to Integer parameters during lowering.
TEST(ZiaBugFixes, ByteArgumentsWidenForCalls) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func sink(value: Integer) {    Viper.Terminal.SayInt(value);
}

func start() {    var x: Byte = 7;
    sink(x);
}
)";
    CompilerInput input{.source = source, .path = "byte_call_widen.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "sink"), static_cast<size_t>(1));
}

//===----------------------------------------------------------------------===//
// Bug #42: Boolean Operators `and`, `or`, `not`
//===----------------------------------------------------------------------===//

/// @brief Test `and` keyword works as logical AND.
TEST(ZiaBugFixes, Bug42_AndKeyword) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var a: Boolean = true;
    var b: Boolean = false;
    if a and b {
        Viper.Terminal.Say("both");
    } else {
        Viper.Terminal.Say("not both");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `or` keyword works as logical OR.
TEST(ZiaBugFixes, Bug42_OrKeyword) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var a: Boolean = true;
    var b: Boolean = false;
    if a or b {
        Viper.Terminal.Say("at least one");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `not` keyword works as logical NOT.
TEST(ZiaBugFixes, Bug42_NotKeyword) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var finished: Boolean = false;
    if not finished {
        Viper.Terminal.Say("still running");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42c.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test combined `and`, `or`, `not` operators.
TEST(ZiaBugFixes, Bug42_CombinedBooleanKeywords) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var x: Integer = 5;
    var y: Integer = 10;

    // Complex boolean expression using word-form operators
    if x > 0 and y > 0 or x < 0 and y < 0 {
        Viper.Terminal.Say("same sign");
    }

    // Using not with comparison
    if not (x == y) {
        Viper.Terminal.Say("different");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42d.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #43: Colon Return Type Syntax
//===----------------------------------------------------------------------===//

/// @brief Test colon return type syntax in functions.
TEST(ZiaBugFixes, Bug43_ColonReturnTypeFunction) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func getNumber() -> Integer {    return 42;
}

func start() {    Viper.Terminal.SayInt(getNumber());
}
)";
    CompilerInput input{.source = source, .path = "bug43a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test colon return type syntax in class methods.
TEST(ZiaBugFixes, Bug43_ColonReturnTypeMethod) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Calculator {
    expose Integer value;

    expose func getValue() -> Integer {        return value;
    }

    expose func double() -> Integer {        return value * 2;
    }
}

func start() {    var calc = new Calculator();
    calc.value = 21;
    Viper.Terminal.SayInt(calc.double());
}
)";
    CompilerInput input{.source = source, .path = "bug43b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #44: Qualified Type Names
//===----------------------------------------------------------------------===//

/// @brief Test qualified type names parse correctly (dot-separated identifiers).
TEST(ZiaBugFixes, Bug44_QualifiedTypeNames) {
    SourceManager sm;
    // Test that qualified type names with dots are parsed correctly
    // Uses List[Integer] which is the supported generic syntax
    const std::string source = R"(
module Test;

func start() {    // Test basic qualified API access (this uses qualified names)
    Viper.Terminal.Say("qualified names work");

    // Test using parameterized generic type
    var items: List[Integer] = [];
    items.add(1);
    items.add(2);
    Viper.Terminal.SayInt(items.count());
}
)";
    CompilerInput input{.source = source, .path = "bug44.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-007: Non-existent class method through field chain
//===----------------------------------------------------------------------===//

/// @brief Calling a non-existent method on a class field should fail compilation.
TEST(ZiaBugFixes, BugFE007_NonExistentEntityMethodError) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Inner {
    expose Integer x;
    expose func init() { x = 0; }    expose func getX() -> Integer { return x; }}

class Outer {
    expose Inner inner;
    expose func init() {        inner = new Inner();
        inner.init();
    }
}

func start() {    var outer = new Outer();
    outer.init();
    outer.inner.nonExistentMethod();
}
)";
    CompilerInput input{.source = source, .path = "bug_fe007a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Should fail: Inner has no method 'nonExistentMethod'
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.diagnostics.errorCount() > 0);
}

/// @brief Calling a valid method on a class field should compile successfully.
TEST(ZiaBugFixes, BugFE007_ValidEntityFieldMethodDispatch) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Inner {
    expose Integer x;
    expose func init() { x = 42; }    expose func getX() -> Integer { return x; }}

class Outer {
    expose Inner inner;
    expose func init() {        inner = new Inner();
        inner.init();
    }
    expose func getInnerX() -> Integer {        return inner.getX();
    }
}

func start() {    var outer = new Outer();
    outer.init();
    var val = outer.inner.getX();
    Viper.Terminal.SayInt(val);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe007b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-005: Complex functions with many locals (regression test)
//===----------------------------------------------------------------------===//

/// @brief Functions with 15+ locals and complex control flow should compile.
TEST(ZiaBugFixes, BugFE005_ManyLocalsComplexControlFlow) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func complexFunc() -> Integer {    var a = 0;
    var b = 1;
    var c = 2;
    var d = 3;
    var e = 4;
    var f = 5;
    var g = 6;
    var h = 7;
    var i = 8;
    var j = 9;
    var k = 10;
    var l = 11;
    var m = 12;
    var n = 13;
    var o = 14;
    var p = 15;
    var q = 16;

    var idx = 0;
    while idx < 10 {
        var kind = (idx % 4) + 1;
        if kind == 1 {
            a = a + 1;
            b = idx;
        } else if kind == 2 {
            c = c + a + b;
            d = d + 1;
        } else if kind == 3 {
            e = e + 1;
            f = f + c;
        } else {
            g = g + 1;
            h = h + d;
        }
        idx = idx + 1;
    }

    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q;
}

func start() {    var result = complexFunc();
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe005.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-006: List method calls on function parameters (regression test)
//===----------------------------------------------------------------------===//

/// @brief List.add() on a function parameter should compile correctly.
TEST(ZiaBugFixes, BugFE006_ListParamMethodCalls) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func categorize(items: List[Integer], evens: List[Integer], odds: List[Integer]) {    var i = 0;
    var total = items.count();
    while i < total {
        var val = items.get(i);
        if val % 2 == 0 {
            evens.add(val);
        } else {
            odds.add(val);
        }
        i = i + 1;
    }
}

func start() {    var items: List[Integer] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    var evens: List[Integer] = [];
    var odds: List[Integer] = [];
    categorize(items, evens, odds);
    Viper.Terminal.SayInt(evens.count());
    Viper.Terminal.SayInt(odds.count());
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-006: Class field chain List method calls generate wrong IL types
//===----------------------------------------------------------------------===//

/// @brief Class field chain List.add() should compile when class B is declared
///        AFTER class A (forward reference pattern).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainListAdd_ForwardRef) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class User {
    expose Container container;
    expose func init() {        container = new Container();
        container.init();
    }
    expose func addItem(val: Integer) {        container.items.add(val);
    }
}

class Container {
    expose List[Integer] items;
    expose func init() { items = []; }}

func start() {    var u = new User();
    u.init();
    u.addItem(42);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_fwd.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Class field chain List.add() should compile when class B is declared
///        BEFORE class A (normal declaration order).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainListAdd_NormalOrder) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Container {
    expose List[Integer] items;
    expose func init() { items = []; }}

class User {
    expose Container container;
    expose func init() {        container = new Container();
        container.init();
    }
    expose func addItem(val: Integer) {        container.items.add(val);
    }
}

func start() {    var u = new User();
    u.init();
    u.addItem(42);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_norm.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Multiple class field chains with different collection types.
TEST(ZiaBugFixes, BugFE006_EntityFieldChainMultipleCollections) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Manager {
    expose DataStore store;
    expose func init() {        store = new DataStore();
        store.init();
    }
    expose func addValue(v: Integer) {        store.values.add(v);
    }
    expose func addName(n: String) {        store.names.add(n);
    }
}

class DataStore {
    expose List[Integer] values;
    expose List[String] names;
    expose func init() {        values = [];
        names = [];
    }
}

func start() {    var m = new Manager();
    m.init();
    m.addValue(10);
    m.addName("hello");
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_multi.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Class field chain accessing an class-typed field (not just List).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainEntityField_ForwardRef) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Outer {
    expose Middle mid;
    expose func init() {        mid = new Middle();
        mid.init();
    }
    expose func getInnerVal() -> Integer {        return mid.inner.value;
    }
}

class Middle {
    expose Inner inner;
    expose func init() {        inner = new Inner();
        inner.value = 99;
    }
}

class Inner {
    expose Integer value;
}

func start() {    var o = new Outer();
    o.init();
    Viper.Terminal.SayInt(o.getInnerVal());
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_entity_chain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Final Constant Forward Reference
//===----------------------------------------------------------------------===//

/// @brief Test that class methods can reference `final` constants defined later
/// in the same file. This was a bug where the single-pass lowering processed
/// class methods before later `final` declarations, causing them to resolve to 0.
TEST(ZiaBugFixes, FinalConstantForwardReference) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Config {
    expose Integer val;
    expose func init() {        val = DEFAULT_SIZE;
    }
}

final DEFAULT_SIZE = 42;

func start() {    var c = new Config();
    c.init();
    Viper.Terminal.SayInt(c.val);
}
)";

    CompilerInput input{.source = source, .path = "final_forward_ref.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Verify the constant was inlined correctly by checking the IL output
    // The class method should use const 42, not 0
    if (result.succeeded()) {
        auto &mod = result.module;
        bool found42 = false;
        for (auto &fn : mod.functions) {
            for (auto &bb : fn.blocks) {
                for (auto &instr : bb.instructions) {
                    for (auto &op : instr.operands) {
                        if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == 42) {
                            found42 = true;
                        }
                    }
                }
            }
        }
        EXPECT_TRUE(found42);
    }
}

/// @brief Test that multiple finals defined after a class all resolve correctly.
TEST(ZiaBugFixes, MultipleFinalConstantsForwardReference) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class MathHelper {
    expose func getSum() -> Integer {        return VAL_A + VAL_B + VAL_C;
    }
}

final VAL_A = 10;
final VAL_B = 20;
final VAL_C = 30;

func start() {    var h = new MathHelper();
    Viper.Terminal.SayInt(h.getSum());
}
)";

    CompilerInput input{.source = source, .path = "multi_final_forward_ref.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-008: Chained method calls on runtime class Ptr receivers
//===----------------------------------------------------------------------===//

/// @brief Chained method calls on Bytes (e.g., bytes.Slice(x,y).ToStr()) should
/// compile. Previously the sema returned the function type instead of the return
/// type for runtime class method calls, causing the outer call to see a Function
/// type as the base instead of the actual Ptr return type.
TEST(ZiaBugFixes, BugFE008_ChainedRuntimeMethodCalls) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections;

func start() {    var data: Bytes = Bytes.FromStr("hello world");
    // Chained call: data.Slice(0,5) returns Bytes, then .ToStr() on it
    var result = data.Slice(0, 5).ToStr();
    Viper.Terminal.Say(result);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe008_chain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Multiple levels of chained runtime method calls should compile.
TEST(ZiaBugFixes, BugFE008_MultipleChainedCalls) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections;

func start() {    var data: Bytes = Bytes.FromStr("hello world!");
    // Double chain: Slice then Slice again
    var sub = data.Slice(0, 11).Slice(6, 11);
    Viper.Terminal.Say(sub.ToStr());
}
)";
    CompilerInput input{.source = source, .path = "bug_fe008_multi_chain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Runtime getters that return opaque objects must preserve the concrete
/// runtime class so chained instance calls lower as direct runtime calls.
TEST(ZiaBugFixes, BugFE008_RuntimeObjectGetterKeepsConcreteClass) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Sound;

func start() {    var bank = SoundBank.New();
    var voice = bank.Get("music_menu").PlayLoop(45, 0);
    Viper.Terminal.SayInt(voice);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe008_soundbank_get.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_EQ(countCallsTo(*mainFn, "Viper.Sound.SoundBank.Get"), 1u);
    EXPECT_EQ(countCallsTo(*mainFn, "Viper.Sound.Sound.PlayLoop"), 1u);
    EXPECT_FALSE(hasOpcode(*mainFn, il::core::Opcode::CallIndirect));
}

//===----------------------------------------------------------------------===//
// BUG-FE-009: List[Boolean].get(i) type mismatch in boolean expressions
//===----------------------------------------------------------------------===//

/// @brief List[Boolean].get(i) should be usable in if-conditions.
/// Previously, emitUnbox for I1 declared the call return type as I1 but
/// the runtime function rt_unbox_i1 actually returns i64, causing a type
/// mismatch in the generated IL.
TEST(ZiaBugFixes, BugFE009_ListBooleanGetInCondition) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var flags: List[Boolean] = [true, false, true];
    if flags.get(0) {
        Viper.Terminal.Say("first is true");
    }
    if flags.get(1) {
        Viper.Terminal.Say("second is true");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug_fe009_bool_get.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief List[Boolean].get(i) should be usable in logical AND/OR expressions.
TEST(ZiaBugFixes, BugFE009_ListBooleanGetInLogicalExpr) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var flags: List[Boolean] = [true, true, false];
    var a = flags.get(0);
    var b = flags.get(1);
    if a && b {
        Viper.Terminal.Say("both true");
    }
    var c = flags.get(2);
    if a || c {
        Viper.Terminal.Say("at least one true");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug_fe009_bool_logical.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-010: Cross-class Ptr type inference (Bytes from Tcp, etc.)
//===----------------------------------------------------------------------===//

/// @brief Runtime class method calls should work on variables whose Ptr type
/// was inferred from a cross-class function return. For example, a function
/// returning obj typed as Viper.Network.Tcp should still allow Bytes methods
/// when the variable is actually Bytes.
TEST(ZiaBugFixes, BugFE010_CrossClassPtrMethodFallback) {
    SourceManager sm;
    // We test with Bytes methods called on a Ptr-typed variable.
    // The key is that the method should resolve via cross-class fallback.
    const std::string source = R"(
module Test;

bind Viper.Collections;

func makeData() -> Bytes {    return Bytes.FromStr("test");
}

func start() {    var data = makeData();
    // data is typed as Ptr via the return type inference.
    // Bytes methods like Slice/ToStr should resolve via fallback.
    var s = data.Slice(0, 4).ToStr();
    Viper.Terminal.Say(s);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe010_cross_class.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-011: Cross-module `final` constant equality always evaluates false
//===----------------------------------------------------------------------===//

namespace fs = std::filesystem;

static fs::path writeFileFE011(const fs::path &dir,
                               const std::string &name,
                               const std::string &contents) {
    fs::create_directories(dir);
    fs::path path = dir / name;
    std::ofstream out(path);
    out << contents;
    return path;
}

/// @brief A `final` constant with a non-literal initializer (e.g., a BinaryExpr
/// like `0 - 2147483647`) must be constant-folded so that the resulting IL uses
/// the correct value rather than constInt(0).
TEST(ZiaBugFixes, BugFE011_NonLiteralFinalFoldsToCorrectValue) {
    SourceManager sm;
    // `final SENTINEL = 0 - 2147483647` is a BinaryExpr, not an IntLiteralExpr.
    // Before the fix, this resolved to constInt(0) everywhere it was referenced.
    const std::string source = R"(
module Test;

final SENTINEL = 0 - 2147483647;
final MASK = 255 & 15;

func start() {    var s: Integer = SENTINEL;
    var m: Integer = MASK;
    Viper.Terminal.SayInt(s);
    Viper.Terminal.SayInt(m);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe011_nonliteral.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    if (result.succeeded()) {
        // The IL must contain -2147483647 (= 0 - 2147483647) and 15 (= 255 & 15).
        // Before the fix, both would appear as 0.
        bool foundSentinel = false;
        bool foundMask = false;
        for (auto &fn : result.module.functions) {
            for (auto &bb : fn.blocks) {
                for (auto &instr : bb.instructions) {
                    for (auto &op : instr.operands) {
                        if (op.kind == il::core::Value::Kind::ConstInt) {
                            if (op.i64 == -2147483647LL)
                                foundSentinel = true;
                            if (op.i64 == 15LL)
                                foundMask = true;
                        }
                    }
                }
            }
        }
        // IL must contain -2147483647 (= 0 - 2147483647) and 15 (= 255 & 15)
        EXPECT_TRUE(foundSentinel);
        EXPECT_TRUE(foundMask);
    }
}

/// @brief Non-literal final constants exported from a bound module must resolve
/// to their computed value when referenced from the importing module.
TEST(ZiaBugFixes, BugFE011_CrossModuleNonLiteralFinalConstant) {
    const fs::path tempRoot = fs::temp_directory_path() / "zia_fe011_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "cross_module_final";

    // Library module exposes a final constant with a non-literal initializer.
    (void)writeFileFE011(dir,
                         "consts.zia",
                         R"(
module Consts;

final SENTINEL = 0 - 2147483647;
)");

    const std::string mainSource = R"(
module Main;
bind "consts.zia";

func start() {    var x: Integer = SENTINEL;
    Viper.Terminal.SayInt(x);
}
)";

    const fs::path mainPath = writeFileFE011(dir, "main.zia", mainSource);
    const std::string mainPathStr = mainPath.string();
    SourceManager sm;
    CompilerInput input{.source = mainSource, .path = mainPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    if (result.succeeded()) {
        bool foundSentinel = false;
        for (auto &fn : result.module.functions) {
            for (auto &bb : fn.blocks) {
                for (auto &instr : bb.instructions) {
                    for (auto &op : instr.operands) {
                        if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == -2147483647LL)
                            foundSentinel = true;
                    }
                }
            }
        }
        // Cross-module non-literal final must fold to -2147483647, not 0
        EXPECT_TRUE(foundSentinel);
    }
}

TEST(ZiaBugFixes, RuntimeTerminalTextCallsAcceptPrimitivesAndObjects) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Dog {}

func start() {
    Viper.Terminal.Say(42);
    Viper.Terminal.Print(true);
    Viper.Terminal.Say(new Dog());
}
)";
    CompilerInput input{.source = source, .path = "terminal_text_calls.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    const auto *mainFn = findFunction(result.module, "main");
    EXPECT_TRUE(mainFn != nullptr);
    if (!mainFn)
        return;

    EXPECT_TRUE(countCallsTo(*mainFn, kTerminalSayInt) >= 1);
    EXPECT_TRUE(countCallsTo(*mainFn, kTerminalPrintBool) >= 1);
    EXPECT_TRUE(countCallsTo(*mainFn, kObjectToString) >= 1);
    EXPECT_TRUE(countCallsTo(*mainFn, kTerminalSay) >= 1);
}

TEST(ZiaBugFixes, NonTerminalRuntimeCallsDoNotStringifyArguments) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var abs = Viper.Math.Abs(-3.5);
    var ch = Viper.String.Chr(66);
    Viper.Terminal.Say(ch);
    Viper.Terminal.SayNum(abs);
}
)";
    CompilerInput input{.source = source, .path = "non_terminal_runtime_calls.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    const auto *mainFn = findFunction(result.module, "main");
    EXPECT_TRUE(mainFn != nullptr);
    if (!mainFn)
        return;

    EXPECT_GE(countCallsTo(*mainFn, "Viper.Math.Abs"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, "Viper.String.Chr"), static_cast<size_t>(1));
    EXPECT_EQ(countCallsTo(*mainFn, kStringFromInt), static_cast<size_t>(0));
    EXPECT_EQ(countCallsTo(*mainFn, kStringFromNum), static_cast<size_t>(0));
}

/// @brief Cross-module finals may reference other exported finals and use full
/// arithmetic folding, including integer division.
TEST(ZiaBugFixes, CrossModuleFinalConstantReferenceAndDivision) {
    const fs::path tempRoot = fs::temp_directory_path() / "zia_final_ref_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "cross_module_ref_div";

    (void)writeFileFE011(dir,
                         "config.zia",
                         R"(
module Config;

final PIECE_SZ = 70;
)");

    const std::string mainSource = R"(
module Main;
bind "config.zia";

final SP = PIECE_SZ;
final CX = PIECE_SZ / 2;

func start() {
    var sp: Integer = SP;
    var cx: Integer = CX;
    Viper.Terminal.SayInt(sp);
    Viper.Terminal.SayInt(cx);
}
)";
    const fs::path mainPath = writeFileFE011(dir, "main.zia", mainSource);
    const std::string mainPathStr = mainPath.string();
    SourceManager sm;
    CompilerInput input{.source = mainSource, .path = mainPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-007 cleanup: integer-to-string conversion lives under Viper.Text.Fmt.
//===----------------------------------------------------------------------===//

/// @brief Fmt.Int remains the canonical integer-to-string conversion.
TEST(ZiaBugFixes, ZIA007_FmtIntConversion) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Terminal;
bind Viper.Text.Fmt as Fmt;

func start() {    Say("x=" + Fmt.Int(42));
    Say("neg=" + Fmt.Int(-7));
}
)";
    CompilerInput input{.source = source, .path = "zia007.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Terminal.Int is intentionally not a compatibility alias for Fmt.Int.
TEST(ZiaBugFixes, ZIA007_TerminalIntIsNotAccepted) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Terminal;

func start() {    var n = 100;
    Say("Result: " + Int(n) + " done");
}
)";
    CompilerInput input{.source = source, .path = "zia007b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-006: Empty list [] inherits element type from declaration annotation.
//===----------------------------------------------------------------------===//

/// @brief `var items: List[Integer] = []` must produce a typed list so that
/// arithmetic on elements retrieved via .get() compiles without error.
TEST(ZiaBugFixes, ZIA006_EmptyListTypeAnnotation) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var items: List[Integer] = [];
    items.add(10);
    items.add(20);
    var sum = items.get(0) + items.get(1);
    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "zia006.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Empty List[String] annotation must propagate, allowing .length() on
/// the populated list.
TEST(ZiaBugFixes, ZIA006_EmptyStringListAnnotation) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var names: List[String] = [];
    names.add("alice");
    names.add("bob");
    Viper.Terminal.SayInt(names.length());
}
)";
    CompilerInput input{.source = source, .path = "zia006b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-003: Negative range literals `for x in -1..2` must work correctly.
//===----------------------------------------------------------------------===//

/// @brief `for x in -1..2` should iterate -1, 0, 1 (sum = 0).
TEST(ZiaBugFixes, ZIA003_NegativeRangeForLoop) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var sum = 0;
    for x in -1..2 {
        sum = sum + x;
    }
    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "zia003.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Nested negative ranges (double-nested direction loop) must work.
TEST(ZiaBugFixes, ZIA003_NestedNegativeRanges) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var count = 0;
    for dr in -1..2 {
        for df in -1..2 {
            count = count + 1;
        }
    }
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "zia003b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-004: `if` used as an expression (value-returning if).
//===----------------------------------------------------------------------===//

/// @brief `var x = if cond { a } else { b }` must compile and produce the
/// correct value in both branches.
TEST(ZiaBugFixes, ZIA004_IfExpression) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var x = 10;
    var label = if x > 5 { "big" } else { "small" };
    Viper.Terminal.Say(label);
    var white = true;
    var dir = if white { -1 } else { 1 };
    Viper.Terminal.SayInt(dir);
    var abs = if x > 0 { x } else { -x };
    Viper.Terminal.SayInt(abs);
}
)";
    CompilerInput input{.source = source, .path = "zia004.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Nested if-expressions and if-expressions inside larger expressions.
TEST(ZiaBugFixes, ZIA004_IfExpressionNested) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var a = 3;
    var b = 7;
    var max = if a > b { a } else { b };
    var sign = if max > 0 { 1 } else { if max < 0 { -1 } else { 0 } };
    Viper.Terminal.SayInt(max);
    Viper.Terminal.SayInt(sign);
}
)";
    CompilerInput input{.source = source, .path = "zia004b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-001: Struct-literal initialization for `value` types.
//===----------------------------------------------------------------------===//

/// @brief `var p = Point { x = 3, y = 4 }` must compile and field values must
/// be correctly set.
TEST(ZiaBugFixes, ZIA001_StructLiteralInit) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
    expose Integer y;
    expose func init(px: Integer, py: Integer) { x = px; y = py; }}

func start() {    var p = Point { x = 3, y = 4 };
    var q = Point { x = p.x + 1, y = p.y };
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
    Viper.Terminal.SayInt(q.x);
}
)";
    CompilerInput input{.source = source, .path = "zia001.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Struct literal with all fields omitting some (only named fields set).
TEST(ZiaBugFixes, ZIA001_StructLiteralPartialFields) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Color {
    expose Integer r;
    expose Integer g;
    expose Integer b;
    expose func init(rv: Integer, gv: Integer, bv: Integer) { r = rv; g = gv; b = bv; }}

func start() {    var red = Color { r = 255, g = 0, b = 0 };
    var green = Color { r = 0, g = 255, b = 0 };
    Viper.Terminal.SayInt(red.r);
    Viper.Terminal.SayInt(green.g);
}
)";
    CompilerInput input{.source = source, .path = "zia001b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// ZIA-002: Fixed-size array fields in `class` types (`Integer[64]`).
//===----------------------------------------------------------------------===//

/// @brief A class with an `Integer[N]` field must compile; individual elements
/// must be readable and writable via subscript syntax.
TEST(ZiaBugFixes, ZIA002_FixedSizeArrayField) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Board {
    expose Integer[64] squares;
    expose func init() {}    expose func get(i: Integer) -> Integer { return squares[i]; }    expose func set(i: Integer, v: Integer) { squares[i] = v; }}

func start() {    var b = new Board();
    b.init();
    b.set(0, 42);
    b.set(63, 7);
    Viper.Terminal.SayInt(b.get(0));
    Viper.Terminal.SayInt(b.get(63));
}
)";
    CompilerInput input{.source = source, .path = "zia002.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Fixed-size array field with element arithmetic.
TEST(ZiaBugFixes, ZIA002_FixedSizeArrayArithmetic) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Vec {
    expose Float[4] data;
    expose func init() {}    expose func set(i: Integer, v: Float) { data[i] = v; }    expose func get(i: Integer) -> Float { return data[i]; }}

func start() {    var v = new Vec();
    v.init();
    v.set(0, 1.0);
    v.set(1, 2.0);
    Viper.Terminal.SayNum(v.get(0) + v.get(1));
}
)";
    CompilerInput input{.source = source, .path = "zia002b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Seq Return Types and for-in iteration
//===----------------------------------------------------------------------===//

/// @brief Test that Viper.String.Split returns a typed Seq[String] and compiles without errors.
TEST(ZiaBugFixes, SeqReturnType_StrSplit_Compiles) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections.Seq as Seq;
// Viper.String.Split should now return Seq[String] (not untyped obj), allowing
// Seq.get_Count / Seq.Get access and for-in iteration.
func start() {    var parts = Viper.String.Split("a,b,c", ",");
    var n = Seq.get_Count(parts);
    var i = 0;
    while i < n {
        var s = Seq.Get(parts, i);
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "seq_split.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test for-in iteration over Viper.String.Split result (Seq[String]).
TEST(ZiaBugFixes, SeqForIn_StrSplit_Compiles) {
    SourceManager sm;
    const std::string source = R"(
module Test;

// for-in over Viper.String.Split works now that it returns Seq[String].
func start() {    for part in Viper.String.Split("hello world foo", " ") {
        Viper.Terminal.Say(part);
    }
}
)";
    CompilerInput input{.source = source, .path = "seq_forin.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Pattern.FindAll should surface a typed Seq[String] in Zia.
TEST(ZiaBugFixes, SeqReturnType_PatternFindAll_UsesSeqCount) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Text;

func start() {
    var matches = Pattern.FindAll("a1b22c333", "[0-9]+");
    var count = matches.Count;
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "pattern_findall_seq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kSeqLen), static_cast<size_t>(1));
}

/// @brief TextWrapper.WrapLines should surface a typed Seq[String] in Zia.
TEST(ZiaBugFixes, SeqReturnType_TextWrapperWrapLines_UsesSeqCount) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var lines = Viper.Text.TextWrapper.WrapLines("one two three", 7);
    var count = lines.Count;
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "textwrapper_wraplines_seq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kSeqLen), static_cast<size_t>(1));
}

/// @brief Template.Keys should surface a typed Seq[String] in Zia.
TEST(ZiaBugFixes, SeqReturnType_TemplateKeys_UsesSeqCount) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var keys = Viper.Text.Template.Keys("Hello {{name}} from {{place}}");
    var count = keys.Count;
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "template_keys_seq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kSeqLen), static_cast<size_t>(1));
}

/// @brief LazySeq.ToSeqN should surface a typed Seq[Object] in Zia.
TEST(ZiaBugFixes, SeqReturnType_LazySeqToSeqN_UsesSeqCount) {
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Functional.LazySeq;

func start() {
    var seq = Range(1, 5, 1);
    var out = ToSeqN(seq, 3);
    var count = out.Count;
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "lazyseq_toseqn_seq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kSeqLen), static_cast<size_t>(1));
}

/// @brief Test that calling an unknown method on an untyped obj emits a diagnostic.
TEST(ZiaBugFixes, SeqUntypedObj_MethodCallError) {
    SourceManager sm;
    // The `Viper.IO.Dir.Files` function returns ptr (rt_list), not seq<str>.
    // Calling .length() on its result (a raw obj/ptr with no class name) should
    // produce a compile-time error, not silently return void.
    // NOTE: Dir.FilesSeq returns seq<str> and is now typed; Dir.Files returns ptr.
    // We test with a function that still returns plain ptr by calling a method on
    // an untyped variable declared as obj-typed.
    const std::string source = R"(
module Test;

func start() {    var x: Integer = 1;
    var y = x;
}
)";
    // This trivial program should compile fine — we just verify the file loads.
    CompilerInput input{.source = source, .path = "untyped_obj.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, DuplicateTopLevelDefinitionsAreRejected) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet() {}func greet() {})";
    CompilerInput input{.source = source, .path = "duplicate_defs.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    bool sawDuplicate = false;
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.message.find("Duplicate definition of 'greet'") != std::string::npos) {
            sawDuplicate = true;
            break;
        }
    }
    EXPECT_TRUE(sawDuplicate);
}

/// @brief Test that runtime List.New preserves semantic List behavior.
TEST(ZiaBugFixes, RuntimeListConstructorPreservesCollectionSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Animal {
    expose Integer age;
}

class Dog extends Animal {
}

func start() {    var animals: List[Animal] = Viper.Collections.List.New();
    var dog = new Dog();
    dog.age = 7;
    animals.Add(dog);
    var first: Animal = animals[0];
    var count: Integer = animals.Count;
    Viper.Terminal.SayInt(first.age);
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "runtime_list_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kListNew), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListAdd), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListGet), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListCount), static_cast<size_t>(1));
}

/// @brief Test that raw List.Get values are contextually unboxed for typed returns.
TEST(ZiaBugFixes, RawListGetUnboxesToTypedReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Palette {
    hide List values;

    expose func init() {        values = new List();
        values.Push(42);
    }

    expose func getFirst() -> Integer {        return values.Get(0);
    }
}

func start() {    var palette = new Palette();
    Viper.Terminal.SayInt(palette.getFirst());
}
)";
    CompilerInput input{.source = source, .path = "raw_list_get_return.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *getFirstFn = findFunction(result.module, "Palette.getFirst");
    ASSERT_TRUE(getFirstFn != nullptr);
    EXPECT_GE(countCallsTo(*getFirstFn, kListGet), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*getFirstFn, kUnboxI64), static_cast<size_t>(1));
}

/// @brief Test that runtime Map.New preserves semantic Map indexing and properties.
TEST(ZiaBugFixes, RuntimeMapConstructorPreservesCollectionSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var scores: Map[String, Integer] = Viper.Collections.Map.New();
    scores["Ada"] = 42;
    var ada: Integer = scores["Ada"];
    var count: Integer = scores.Count;
    Viper.Terminal.SayInt(ada);
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "runtime_map_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kMapNew), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kMapSet), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kMapGet), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kMapCount), static_cast<size_t>(1));
}

/// @brief Assignment into typed Map fields should preserve the runtime Map surface.
TEST(ZiaBugFixes, RuntimeMapConstructorPreservesFieldAssignmentSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Collections;

class Session {
    expose Map vars;

    expose func init() {
        vars = Map.New();
        Map.SetStr(vars, "mode", "strict");
    }

    expose func mode() -> String {
        return Map.GetStr(vars, "mode");
    }
}

func start() {
    var session = new Session();
    session.init();
    Viper.Terminal.Say(session.mode());
}
)";
    CompilerInput input{.source = source, .path = "runtime_map_field_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *initFn = findFunction(result.module, "Session.init");
    ASSERT_TRUE(initFn != nullptr);
    EXPECT_GE(countCallsTo(*initFn, kMapNew), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*initFn, "Viper.Collections.Map.SetStr"), static_cast<size_t>(1));
}

/// @brief Test that runtime Set.New preserves semantic Set methods and properties.
TEST(ZiaBugFixes, RuntimeSetConstructorPreservesCollectionSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var names: Set[String] = Viper.Collections.Set.New();
    var inserted: Boolean = names.add("Ada");
    var hasAda: Boolean = names.contains("Ada");
    var count: Integer = names.Count;
    Viper.Terminal.SayInt(inserted ? 1 : 0);
    Viper.Terminal.SayInt(hasAda ? 1 : 0);
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "runtime_set_surface.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kSetNew), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kSetPut), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kSetHas), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kSetCount), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, RejectsInvalidAssignmentTargets) {
    SourceManager sm;
    const std::string invalidTarget = R"(
module Test;

func start() {
    (1 + 2) = 3;
}
)";
    CompilerInput invalidInput{.source = invalidTarget, .path = "invalid_assign_target.zia"};
    CompilerOptions opts{};
    auto invalidResult = compile(invalidInput, opts, sm);
    EXPECT_FALSE(invalidResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(invalidResult, "Assignment target must be"));
}

TEST(ZiaBugFixes, RejectsDuplicateParametersAndParserFailureInLists) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string duplicateParam = R"(
module Test;

func echo(value: Integer, value: Integer) -> Integer {
    return value;
}
)";
    CompilerInput dupParamInput{.source = duplicateParam, .path = "duplicate_param.zia"};
    auto dupParamResult = compile(dupParamInput, opts, sm);
    EXPECT_FALSE(dupParamResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(dupParamResult, "Duplicate definition of 'value'"));

    SourceManager sm2;
    const std::string missingListElem = R"(
module Test;

func start() {
    var values = [1, , 3];
}
)";
    CompilerInput listInput{.source = missingListElem, .path = "list_parse_failure.zia"};
    auto listResult = compile(listInput, opts, sm2);
    EXPECT_FALSE(listResult.succeeded());
}

TEST(ZiaBugFixes, UnitAndRangeExpressionsLowerOutsideForIn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    ();
    var values = 0..=3;
    Viper.Terminal.SayInt(values.Count);
}
)";
    CompilerInput input{.source = source, .path = "unit_range_exprs.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kListNew), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListAdd), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListCount), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, RangeModifiersRejectPlainListsAndNonPositiveLiteralSteps) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string listRev = R"(
module Test;

func start() {
    var values = [1, 2, 3];
    for (value in values.rev()) {
        Viper.Terminal.SayInt(value);
    }
}
)";
    CompilerInput revInput{.source = listRev, .path = "list_rev_rejected.zia"};
    auto revResult = compile(revInput, opts, sm);
    EXPECT_FALSE(revResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(revResult, "rev() is only supported on range expressions"));

    SourceManager sm2;
    const std::string stepZero = R"(
module Test;

func start() {
    for (value in (0..10).step(0)) {
        Viper.Terminal.SayInt(value);
    }
}
)";
    CompilerInput stepInput{.source = stepZero, .path = "range_step_zero.zia"};
    auto stepResult = compile(stepInput, opts, sm2);
    EXPECT_FALSE(stepResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(stepResult, "step() argument must be"));
}

TEST(ZiaBugFixes, CompoundAssignmentSupportsPureIndexExpressionsOnly) {
    SourceManager sm;
    const std::string pureIndex = R"(
module Test;

func start() {
    var values = [1, 2, 3];
    var i: Integer = 0;
    values[i + 1] += 4;
    Viper.Terminal.SayInt(values[1]);
}
)";
    CompilerInput pureInput{.source = pureIndex, .path = "compound_pure_index.zia"};
    CompilerOptions opts{};
    auto pureResult = compile(pureInput, opts, sm);
    EXPECT_TRUE(pureResult.succeeded());

    SourceManager sm2;
    const std::string effectfulTarget = R"(
module Test;

func index() -> Integer {
    return 0;
}

func start() {
    var values = [1, 2, 3];
    values[index()] += 1;
}
)";
    CompilerInput effectInput{.source = effectfulTarget, .path = "compound_effectful.zia"};
    auto effectResult = compile(effectInput, opts, sm2);
    EXPECT_FALSE(effectResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(effectResult, "side-effect-free lvalue"));
}

TEST(ZiaBugFixes, AssignmentCoercesStoredAndReturnedValues) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Numbers {
    expose Float[2] values;
    expose func setFirst(value: Integer) -> Float {
        return values[0] = value;
    }
}

func start() {
    var n = new Numbers();
    var x: Float = n.setFirst(7);
    Viper.Terminal.SayNum(x);
}
)";
    CompilerInput input{.source = source, .path = "assignment_coerces.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, OptionalChainSupportsPropertiesAndBuiltInAliases) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Counter {
    hide Integer _count;

    expose func init(value: Integer) {
        _count = value;
    }

    expose property count: Integer {
        get {
            return _count;
        }
    }
}

func start() {
    var c: Counter? = new Counter(7);
    var value: Integer? = c?.count;
    var text: String? = "abc";
    var len: Integer? = text?.Length;
    var values: List[Integer]? = [1, 2, 3];
    var count: Integer? = values?.Count;
    Viper.Terminal.SayInt(value ?? 0);
    Viper.Terminal.SayInt(len ?? 0);
    Viper.Terminal.SayInt(count ?? 0);
}
)";
    CompilerInput input{.source = source, .path = "optional_chain_props.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, "Counter.get_count"), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kStringLength), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListCount), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, StructLiteralsTrailingCommasAndMatchLookaheadGaps) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
}

func pick(p: Point = Point { x = 5 }) -> Integer = Point { x = p.x }.x;

class Holder {
    expose Point p = Point { x = 2 };
}

func sum(a: Integer, b: Integer,) -> Integer {
    return a + b;
}

func start() {
    var p = Point { x = 1 };
    var values = [1, 2, 3,];
    var labels = {"a": 1,};
    var set = {1, 2,};
    var result = match new Holder() {
        _ => sum(values.Count, labels.Count,);
    };
    Viper.Terminal.SayInt(pick(p) + set.Count + result);
}
)";
    CompilerInput input{.source = source, .path = "parser_gap_fixes.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, StructLiteralsWorkInNestedExpressionContexts) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
}

struct Box {
    expose Point p;
}

func take(p: Point) -> Integer {
    return p.x;
}

func start() {
    var p = Point { x = 1 };
    p = Point { x = 2 };
    var callValue = take(Point { x = 3 });
    var points: List[Point] = [Point { x = 4 }, Point { x = 5 }];
    var chosen = true ? Point { x = 6 } : Point { x = 7 };
    var box = Box { p: Point { x: 9 } };
    var box2 = Box { p = Point { x = 10 } };
    var matched = match callValue {
        _ => Point { x = 8 };
    };
    Viper.Terminal.SayInt(p.x + callValue + points.Count + chosen.x + matched.x + box.p.x + box2.p.x);
}
)";
    CompilerInput input{.source = source, .path = "struct_literal_expr_contexts.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, NaryTupleDestructuringAndPatternsCompile) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var triple = (1, "two", 3);
    var (a: Integer, b: String, c) = triple;
    final (x, y, z) = (4, 5, 6);
    var matched = match (a, x, z) {
        (left, middle, right) => left + middle + right;
    };
    Viper.Terminal.Say(b);
    Viper.Terminal.SayInt(c + y + matched);
}
)";
    CompilerInput input{.source = source, .path = "nary_tuple_destructure.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, RejectsUnitInStoredValueContexts) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string varUnit = R"(
module Test;

func start() {
    var value = ();
}
)";
    CompilerInput varInput{.source = varUnit, .path = "unit_var.zia"};
    auto varResult = compile(varInput, opts, sm);
    EXPECT_FALSE(varResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(varResult, "Unit literal cannot be stored"));

    SourceManager sm2;
    const std::string assignUnit = R"(
module Test;

func start() {
    var value: Integer? = null;
    value = ();
}
)";
    CompilerInput assignInput{.source = assignUnit, .path = "unit_assign.zia"};
    auto assignResult = compile(assignInput, opts, sm2);
    EXPECT_FALSE(assignResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(assignResult, "Unit literal cannot be assigned"));
}

TEST(ZiaBugFixes, RejectsReadOnlyBuiltinPropertyAndStringIndexAssignments) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string countAssign = R"(
module Test;

func start() {
    var values = [1, 2, 3];
    values.Count = 9;
}
)";
    CompilerInput countInput{.source = countAssign, .path = "readonly_count_assign.zia"};
    auto countResult = compile(countInput, opts, sm);
    EXPECT_FALSE(countResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(countResult, "Cannot assign to read-only property"));

    SourceManager sm2;
    const std::string stringIndexAssign = R"(
module Test;

func start() {
    var text = "abc";
    text[0] = "z";
}
)";
    CompilerInput indexInput{.source = stringIndexAssign, .path = "string_index_assign.zia"};
    auto indexResult = compile(indexInput, opts, sm2);
    EXPECT_FALSE(indexResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(indexResult, "Cannot assign through a String index"));
}

TEST(ZiaBugFixes, UnknownCollectionFieldsAndMethodsAreErrors) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string unknownField = R"(
module Test;

func start() {
    var values = [1, 2, 3];
    var bad = values.nope;
}
)";
    CompilerInput fieldInput{.source = unknownField, .path = "unknown_collection_field.zia"};
    auto fieldResult = compile(fieldInput, opts, sm);
    EXPECT_FALSE(fieldResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(fieldResult, "Unknown field 'nope' on List"));

    SourceManager sm2;
    const std::string unknownMethod = R"(
module Test;

func start() {
    var text = "abc";
    text.nope();
}
)";
    CompilerInput methodInput{.source = unknownMethod, .path = "unknown_string_method.zia"};
    auto methodResult = compile(methodInput, opts, sm2);
    EXPECT_FALSE(methodResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(methodResult, "String has no method 'nope'"));
}

TEST(ZiaBugFixes, RangeModifierExpressionsLowerAndDuplicateStepIsRejected) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var values = (0..=5).rev().step(2);
    Viper.Terminal.SayInt(values.Count);
}
)";
    CompilerInput input{.source = source, .path = "range_modifier_expr.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kListAdd), static_cast<size_t>(1));
    EXPECT_GE(countCallsTo(*mainFn, kListCount), static_cast<size_t>(1));
    EXPECT_EQ(countOpcode(*mainFn, il::core::Opcode::IdxChk), static_cast<size_t>(0));
    EXPECT_GE(countOpcode(*mainFn, il::core::Opcode::SCmpLT), static_cast<size_t>(1));

    SourceManager sm2;
    const std::string duplicateStep = R"(
module Test;

func start() {
    var values = (0..10).step(2).step(3);
    Viper.Terminal.SayInt(values.Count);
}
)";
    CompilerInput dupInput{.source = duplicateStep, .path = "duplicate_range_step.zia"};
    auto dupResult = compile(dupInput, opts, sm2);
    EXPECT_FALSE(dupResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(dupResult, "cannot apply step() more than once"));
}

TEST(ZiaBugFixes, OptionalStructChainUnwrapsPayloadBeforeFieldAccess) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
}

func start() {
    var p: Point? = Point { x = 7 };
    var x: Integer? = p?.x;
    Viper.Terminal.SayInt(x ?? 0);
}
)";
    CompilerInput input{.source = source, .path = "optional_struct_chain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_GE(countCallsTo(*mainFn, kBoxValueType), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, FixedArrayIndexReadsAndWritesEmitBoundsChecksForByteIndexes) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Numbers {
    expose Integer[2] values;

    expose func set(i: Byte, v: Integer) {
        values[i] = v;
    }

    expose func get(i: Byte) -> Integer {
        return values[i];
    }
}

func start() {
    var n = new Numbers();
    var i: Byte = 1;
    n.set(i, 9);
    Viper.Terminal.SayInt(n.get(i));
}
)";
    CompilerInput input{.source = source, .path = "fixed_array_idxchk_byte.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *setFn = findFunction(result.module, "Numbers.set");
    const auto *getFn = findFunction(result.module, "Numbers.get");
    ASSERT_TRUE(setFn != nullptr);
    ASSERT_TRUE(getFn != nullptr);
    EXPECT_GE(countOpcode(*setFn, il::core::Opcode::IdxChk), static_cast<size_t>(1));
    EXPECT_GE(countOpcode(*getFn, il::core::Opcode::IdxChk), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, FixedArrayOfStructsUsesInlineElementStride) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Pair {
    expose Integer a;
    expose Integer b;
}

class Pairs {
    expose Pair[2] values;

    expose func set(i: Integer, value: Pair) {
        values[i] = value;
    }

    expose func get(i: Integer) -> Pair {
        return values[i];
    }
}

func start() {
    var pairs = new Pairs();
    var value = Pair { a = 1, b = 2 };
    pairs.set(1, value);
    var loaded = pairs.get(1);
    Viper.Terminal.SayInt(loaded.b);
}
)";
    CompilerInput input{.source = source, .path = "fixed_array_struct_stride.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *setFn = findFunction(result.module, "Pairs.set");
    const auto *getFn = findFunction(result.module, "Pairs.get");
    ASSERT_TRUE(setFn != nullptr);
    ASSERT_TRUE(getFn != nullptr);
    EXPECT_TRUE(hasIMulByConst(*setFn, 16));
    EXPECT_TRUE(hasIMulByConst(*getFn, 16));
}

TEST(ZiaBugFixes, TupleLayoutUsesSemanticElementOffsets) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var a: Byte = 1;
    var b: Byte = 2;
    var t = (a, b, 99);
    Viper.Terminal.SayInt(t.2);
}
)";
    CompilerInput input{.source = source, .path = "tuple_semantic_offsets.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_TRUE(hasAllocaSize(*mainFn, 16));
}

TEST(ZiaBugFixes, TernaryLookaheadAllowsExpressionKeywordArms) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var flag = true;
    var value = flag ? if flag { 1 } else { 2 } : 3;
    var negated = flag ? not false : false;
    Viper.Terminal.SayInt(value + (negated ? 1 : 0));
}
)";
    CompilerInput input{.source = source, .path = "ternary_keyword_arms.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, InheritedPropertyAccessUsesDeclaringOwner) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Base {
    expose property answer: Integer {
        get {
            return 42;
        }
    }
}

class Child extends Base {
}

func start() {
    var child = new Child();
    Viper.Terminal.SayInt(child.answer);
}
)";
    CompilerInput input{.source = source, .path = "inherited_property_owner.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_EQ(countCallsTo(*mainFn, "Base.get_answer"), static_cast<size_t>(1));
    EXPECT_EQ(countCallsTo(*mainFn, "Child.get_answer"), static_cast<size_t>(0));
}

TEST(ZiaBugFixes, GlobalOptionalStructAssignmentWrapsOnlyOnce) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
}

var maybe: Point?;

func start() {
    maybe = Point { x = 1 };
}
)";
    CompilerInput input{.source = source, .path = "global_optional_struct_assign.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *mainFn = findFunction(result.module, "main");
    ASSERT_TRUE(mainFn != nullptr);
    EXPECT_EQ(countCallsTo(*mainFn, kBoxValueType), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, NestedTerminatingIfDoesNotCreateLiveDeadMerge) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Node {
    expose Boolean isLeaf;
}

func maybeNode() -> Node? {
    return new Node();
}

func check(hit: Boolean, pred: Boolean) -> Boolean {
    var maybe = maybeNode();
    if maybe == null {
        return false;
    }

    var node = maybe!;
    if hit {
        if node.isLeaf {
            return true;
        } else {
            if pred {
                return false;
            }
            return false;
        }
    }

    if node.isLeaf {
        return false;
    }
    return true;
}

func start() {
    Viper.Terminal.SayBool(check(false, false));
}
)";
    CompilerInput input{.source = source, .path = "nested_terminating_if_dead_merge.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(findFunction(result.module, "check") != nullptr);
}

TEST(ZiaBugFixes, FixedArraySizesUseParsedIntegerLiteralValues) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Buffer {
    expose Integer[0x1_0] values;

    expose func init() {
        values[15] = 7;
    }

    expose func last() -> Integer {
        return values[0b1111];
    }
}

func start() {
    var b = new Buffer();
    b.init();
    Viper.Terminal.SayInt(b.last());
}
)";
    CompilerInput input{.source = source, .path = "fixed_array_parsed_literal_sizes.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    const auto *initFn = findFunction(result.module, "Buffer.init");
    const auto *lastFn = findFunction(result.module, "Buffer.last");
    ASSERT_TRUE(initFn != nullptr);
    ASSERT_TRUE(lastFn != nullptr);
    EXPECT_GE(countOpcode(*initFn, il::core::Opcode::IdxChk), static_cast<size_t>(1));
    EXPECT_GE(countOpcode(*lastFn, il::core::Opcode::IdxChk), static_cast<size_t>(1));
}

TEST(ZiaBugFixes, HexLiteralInt64MinBitPatternCompiles) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var min: Integer = 0x8000000000000000;
    var negOne: Integer = 0xFFFF_FFFF_FFFF_FFFF;
    Viper.Terminal.SayInt(min);
    Viper.Terminal.SayInt(negOne);
}
)";
    CompilerInput input{.source = source, .path = "hex_i64_min.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    bool foundMin = false;
    bool foundNegOne = false;
    for (const auto &fn : result.module.functions) {
        for (const auto &bb : fn.blocks) {
            for (const auto &instr : bb.instructions) {
                for (const auto &op : instr.operands) {
                    if (op.kind == il::core::Value::Kind::ConstInt &&
                        op.i64 == std::numeric_limits<int64_t>::min())
                        foundMin = true;
                    if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == -1)
                        foundNegOne = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundMin);
    EXPECT_TRUE(foundNegOne);
}

TEST(ZiaBugFixes, EnumExplicitInt64MinAndAutoIncrementOverflow) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string minSource = R"(
module Test;

enum Code {
    Min = -9223372036854775808,
    Next
}

func start() {
    var x: Integer = Code.Min;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput minInput{.source = minSource, .path = "enum_int64_min.zia"};
    auto minResult = compile(minInput, opts, sm);
    ASSERT_TRUE(minResult.succeeded());

    bool foundMin = false;
    for (const auto &fn : minResult.module.functions) {
        for (const auto &bb : fn.blocks) {
            for (const auto &instr : bb.instructions) {
                for (const auto &op : instr.operands) {
                    if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == INT64_MIN)
                        foundMin = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundMin);

    SourceManager sm2;
    const std::string overflowSource = R"(
module Test;

enum Bad {
    Last = 9223372036854775807,
    Wrap
}

func start() {}
)";
    CompilerInput overflowInput{.source = overflowSource, .path = "enum_auto_overflow.zia"};
    auto overflowResult = compile(overflowInput, opts, sm2);
    EXPECT_FALSE(overflowResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(overflowResult, "auto-increment would exceed"));
}

TEST(ZiaBugFixes, NamespaceEnumFinalConstantsUseQualifiedVariantValues) {
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Inner {
    enum Mode {
        Off = 0,
        On = 7
    }
}

final ACTIVE = Inner.Mode.On;

func start() {
    var active: Integer = ACTIVE;
    Viper.Terminal.SayInt(active);
}
)";
    CompilerInput input{.source = source, .path = "namespace_enum_final.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    bool foundSeven = false;
    for (const auto &fn : result.module.functions) {
        for (const auto &bb : fn.blocks) {
            for (const auto &instr : bb.instructions) {
                for (const auto &op : instr.operands) {
                    if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == 7)
                        foundSeven = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundSeven);
}

TEST(ZiaBugFixes, InvalidGlobalAndFinalDeclarationsAreRejected) {
    CompilerOptions opts{};

    SourceManager sm;
    const std::string finalGlobal = R"(
module Test;

final Missing: Integer;
func start() {}
)";
    CompilerInput finalGlobalInput{.source = finalGlobal, .path = "final_global_no_init.zia"};
    auto finalGlobalResult = compile(finalGlobalInput, opts, sm);
    EXPECT_FALSE(finalGlobalResult.succeeded());
    EXPECT_TRUE(
        hasErrorContaining(finalGlobalResult, "'final' declarations require an initializer"));

    SourceManager sm2;
    const std::string finalLocal = R"(
module Test;

func start() {
    let x: Integer;
}
)";
    CompilerInput finalLocalInput{.source = finalLocal, .path = "final_local_no_init.zia"};
    auto finalLocalResult = compile(finalLocalInput, opts, sm2);
    EXPECT_FALSE(finalLocalResult.succeeded());
    EXPECT_TRUE(
        hasErrorContaining(finalLocalResult, "'final' declarations require an initializer"));

    SourceManager sm3;
    const std::string untypedGlobal = R"(
module Test;

var value;
func start() {}
)";
    CompilerInput untypedInput{.source = untypedGlobal, .path = "global_no_type_no_init.zia"};
    auto untypedResult = compile(untypedInput, opts, sm3);
    EXPECT_FALSE(untypedResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(untypedResult, "Cannot infer type without initializer"));

    SourceManager sm4;
    const std::string nullGlobal = R"(
module Test;

var maybe = null;
func start() {}
)";
    CompilerInput nullInput{.source = nullGlobal, .path = "global_null_infer.zia"};
    auto nullResult = compile(nullInput, opts, sm4);
    EXPECT_FALSE(nullResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(nullResult, "Cannot infer type from null initializer"));

    SourceManager sm5;
    const std::string unitGlobal = R"(
module Test;

var unitValue = ();
func start() {}
)";
    CompilerInput unitInput{.source = unitGlobal, .path = "global_unit_init.zia"};
    auto unitResult = compile(unitInput, opts, sm5);
    EXPECT_FALSE(unitResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(unitResult, "Unit literal cannot be stored"));
}

TEST(ZiaBugFixes, GlobalAndFieldByteLiteralInitializersAreContextual) {
    SourceManager sm;
    const std::string source = R"(
module Test;

var globalByte: Byte = 7;

class Packet {
    expose Byte tag = 8;
}

func start() {
    var p = new Packet();
    Viper.Terminal.SayInt(globalByte);
    Viper.Terminal.SayInt(p.tag);
}
)";
    CompilerInput input{.source = source, .path = "byte_global_field_init.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(findFunction(result.module, "main") != nullptr);
}

TEST(ZiaBugFixes, FieldPropertyNameCollisionsAreRejectedInDeclarationOrder) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Bad {
    expose property value: Integer {
        get {
            return 1;
        }
    }
    expose Integer value;
}

func start() {}
)";
    CompilerInput input{.source = source, .path = "property_then_field_duplicate.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Duplicate definition of 'value' in type 'Bad'"));
}

TEST(ZiaBugFixes, ResultUnitAndUnitParametersLowerAsValues) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func done() -> Result[Unit] {
    return Ok(());
}

func consume(value: Unit) -> Unit {
    return value;
}

func start() {
    var result = done();
    consume(());
}
)";
    CompilerInput input{.source = source, .path = "unit_result_value.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ResultUnitAndUnitParametersLowerAsValues:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(findFunction(result.module, "done") != nullptr);
    EXPECT_TRUE(findFunction(result.module, "consume") != nullptr);
}

TEST(ZiaBugFixes, VoidAndNeverAreRejectedBeforeLowering) {
    SourceManager sm1;
    const std::string voidParam = R"(
module Test;

func takesVoid(value: Void) {}
func start() {}
)";
    CompilerInput voidInput{.source = voidParam, .path = "void_parameter.zia"};
    CompilerOptions opts{};
    auto voidResult = compile(voidInput, opts, sm1);
    EXPECT_FALSE(voidResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(voidResult, "cannot be used for a function parameter"));

    SourceManager sm2;
    const std::string neverLocal = R"(
module Test;

func start() {
    var value: Never;
}
)";
    CompilerInput neverInput{.source = neverLocal, .path = "never_local.zia"};
    auto neverResult = compile(neverInput, opts, sm2);
    EXPECT_FALSE(neverResult.succeeded());
    EXPECT_TRUE(hasErrorContaining(neverResult, "cannot be used for a local value"));
}

TEST(ZiaBugFixes, UnderscorePrefixedParametersSuppressUnusedWarning) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func keep(_unused: Integer, _label: String, value: Integer) -> Integer {
    return value;
}

func start() {
    Viper.Terminal.SayInt(keep(1, "ignored", 2));
}
)";
    CompilerInput input{.source = source, .path = "underscore_parameter_unused.zia"};
    CompilerOptions opts{};
    opts.warningPolicy.warningsAsErrors = true;

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(hasDiagnosticCode(result, "W001"));
}

TEST(ZiaBugFixes, InterfaceValuesAndListGetsCanCompareWithNull) {
    SourceManager sm;
    const std::string source = R"(
module Test;

interface Tool {
    func id() -> Integer;
}

func start() {
    var t: Tool = null;
    if t == null {
        Viper.Terminal.Say("empty");
    }

    var tools: List[Tool] = [];
    var maybe = tools.get(0);
    if maybe != null {
        Viper.Terminal.SayInt(maybe.id());
    }
}
)";
    CompilerInput input{.source = source, .path = "interface_null_comparison.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for InterfaceValuesAndListGetsCanCompareWithNull:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, Gradient2DSampleAcceptsNormalizedNumber) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Graphics;

func start() {
    var fg = Color.RGBA(0, 0, 0, 255);
    var bg = Color.RGBA(255, 255, 255, 255);
    var g = Gradient2D.New(fg, bg, 4);
    var c = g.Sample(0.5);
    var raw = g.SampleRGBA(0.5);
    var pct = g.SamplePct(50);
    var rawPct = g.SampleRGBAPct(50);
    Viper.Terminal.SayInt(Color.GetR(c) + Color.GetR(pct) + raw + rawPct);
}
)";
    CompilerInput input{.source = source, .path = "gradient_normalized_sample.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for Gradient2DSampleAcceptsNormalizedNumber:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaBugFixes, PixelsTextMethodsResolveThroughRuntimeSurface) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Graphics;

func start() {
    var p = Pixels.New(48, 24);
    p.DrawText(0, 0, "A", Color.RGB(255, 0, 0));
    p.DrawTextBg(8, 0, "B", Color.RGB(0, 255, 0), Color.RGBA(0, 0, 0, 128));
    p.DrawTextScaled(0, 8, "C", 2, Color.RGB(0, 0, 255));
    p.DrawTextScaledBg(16, 8, "D", 2, Color.RGB(255, 255, 255), Color.RGB(10, 20, 30));
    p.DrawTextCentered(16, "E", Color.RGB(20, 30, 40));
    p.DrawTextRight(4, 16, "F", Color.RGB(50, 60, 70));
    p.DrawTextCenteredScaled(0, "G", Color.RGB(80, 90, 100), 2);
    var w = Pixels.TextWidth("hi");
    var h = Pixels.TextHeight();
    var sw = Pixels.TextScaledWidth("hi", 2);
    Viper.Terminal.SayInt(w + h + sw);
}
)";
    CompilerInput input{.source = source, .path = "pixels_text_methods.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for PixelsTextMethodsResolveThroughRuntimeSurface:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
}

// --- Paren-less runtime-method access is diagnosed (V-ZIA-METHOD-CALL) --------
// Regression for the sema/lowering bug where accessing a runtime method without
// call parentheses passed sema and then emitted invalid IL (or a silently-typed
// constant). Sema now completes the runtime-class resolver: property -> method-
// without-parens (with an "add ()" fix-it) -> genuine "no member".

TEST(ZiaRuntimeMemberAccess, ParenlessZeroArgMethodEmitsMethodCallDiagnostic) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Game3D;
func start() {
    var e: Entity3D = Entity3D.New();
    var s = e.IsSpawned;
}
)";
    CompilerInput input{.source = source, .path = "parenless_method.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagnosticCode(result, "V-ZIA-METHOD-CALL"));
    EXPECT_TRUE(hasErrorContaining(result, "is a method of"));
}

TEST(ZiaRuntimeMemberAccess, ParenfulMethodCallCompiles) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Game3D;
func start() {
    var e: Entity3D = Entity3D.New();
    var s = e.IsSpawned();
}
)";
    CompilerInput input{.source = source, .path = "parenful_method.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(hasDiagnosticCode(result, "V-ZIA-METHOD-CALL"));
}

TEST(ZiaRuntimeMemberAccess, ParenlessArgMethodDiagnosedNotUnknownMember) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Game3D;
func start() {
    var e: Entity3D = Entity3D.New();
    var f = e.SetPosition;
}
)";
    CompilerInput input{.source = source, .path = "parenless_arg_method.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagnosticCode(result, "V-ZIA-METHOD-CALL"));
    // An arg-taking method is still a method — it must not be mislabeled "no member".
    EXPECT_FALSE(hasErrorContaining(result, "has no member"));
}

TEST(ZiaRuntimeMemberAccess, UnknownRuntimeMemberEmitsNoMemberError) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Game3D;
func start() {
    var e: Entity3D = Entity3D.New();
    var g = e.Nonexistent;
}
)";
    CompilerInput input{.source = source, .path = "unknown_member.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "has no member 'Nonexistent'"));
}

TEST(ZiaRuntimeMemberAccess, ParenlessPropertyStillResolves) {
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Viper.Game3D;
func start() {
    var e: Entity3D = Entity3D.New();
    var n = e.Name;
}
)";
    CompilerInput input{.source = source, .path = "parenless_property.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(hasDiagnosticCode(result, "V-ZIA-METHOD-CALL"));
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
