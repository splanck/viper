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
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include "tests/common/PosixCompat.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Bug #38: Module-Level Mutable Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level mutable variables can be read and written.
TEST(ZiaBugFixes, Bug38_ModuleLevelMutableVariables)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

var counter: Integer;

func start() {
    counter = 10;
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
TEST(ZiaBugFixes, Bug38_ModuleLevelVarNoInitializer)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

var running: Boolean;
var score: Integer;

func start() {
    running = true;
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

//===----------------------------------------------------------------------===//
// Bug #39: Module-Level Entity Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level entity variables can store and retrieve objects.
TEST(ZiaBugFixes, Bug39_ModuleLevelEntityVariables)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Player {
    expose Integer score;

    expose func addScore(Integer points) {
        score = score + points;
    }
}

var player: Player;

func start() {
    player = new Player();
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

//===----------------------------------------------------------------------===//
// Bug #42: Boolean Operators `and`, `or`, `not`
//===----------------------------------------------------------------------===//

/// @brief Test `and` keyword works as logical AND.
TEST(ZiaBugFixes, Bug42_AndKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
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
TEST(ZiaBugFixes, Bug42_OrKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
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
TEST(ZiaBugFixes, Bug42_NotKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean finished = false;
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
TEST(ZiaBugFixes, Bug42_CombinedBooleanKeywords)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    Integer y = 10;

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
TEST(ZiaBugFixes, Bug43_ColonReturnTypeFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func getNumber(): Integer {
    return 42;
}

func start() {
    Viper.Terminal.SayInt(getNumber());
}
)";
    CompilerInput input{.source = source, .path = "bug43a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test colon return type syntax in entity methods.
TEST(ZiaBugFixes, Bug43_ColonReturnTypeMethod)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Calculator {
    expose Integer value;

    expose func getValue(): Integer {
        return value;
    }

    expose func double(): Integer {
        return value * 2;
    }
}

func start() {
    var calc = new Calculator();
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
TEST(ZiaBugFixes, Bug44_QualifiedTypeNames)
{
    SourceManager sm;
    // Test that qualified type names with dots are parsed correctly
    // Uses List[Integer] which is the supported generic syntax
    const std::string source = R"(
module Test;

func start() {
    // Test basic qualified API access (this uses qualified names)
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
// BUG-FE-007: Non-existent entity method through field chain
//===----------------------------------------------------------------------===//

/// @brief Calling a non-existent method on an entity field should fail compilation.
TEST(ZiaBugFixes, BugFE007_NonExistentEntityMethodError)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Inner {
    expose Integer x;
    expose func init() { x = 0; }
    expose func getX() -> Integer { return x; }
}

entity Outer {
    expose Inner inner;
    expose func init() {
        inner = new Inner();
        inner.init();
    }
}

func start() {
    var outer = new Outer();
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

/// @brief Calling a valid method on an entity field should compile successfully.
TEST(ZiaBugFixes, BugFE007_ValidEntityFieldMethodDispatch)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Inner {
    expose Integer x;
    expose func init() { x = 42; }
    expose func getX() -> Integer { return x; }
}

entity Outer {
    expose Inner inner;
    expose func init() {
        inner = new Inner();
        inner.init();
    }
    expose func getInnerX() -> Integer {
        return inner.getX();
    }
}

func start() {
    var outer = new Outer();
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
TEST(ZiaBugFixes, BugFE005_ManyLocalsComplexControlFlow)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func complexFunc() -> Integer {
    var a = 0;
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

func start() {
    var result = complexFunc();
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
TEST(ZiaBugFixes, BugFE006_ListParamMethodCalls)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func categorize(items: List[Integer], evens: List[Integer], odds: List[Integer]) {
    var i = 0;
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

func start() {
    var items: List[Integer] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
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
// BUG-FE-006: Entity field chain List method calls generate wrong IL types
//===----------------------------------------------------------------------===//

/// @brief Entity field chain List.add() should compile when entity B is declared
///        AFTER entity A (forward reference pattern).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainListAdd_ForwardRef)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity User {
    expose Container container;
    expose func init() {
        container = new Container();
        container.init();
    }
    expose func addItem(val: Integer) {
        container.items.add(val);
    }
}

entity Container {
    expose List[Integer] items;
    expose func init() { items = []; }
}

func start() {
    var u = new User();
    u.init();
    u.addItem(42);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_fwd.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Entity field chain List.add() should compile when entity B is declared
///        BEFORE entity A (normal declaration order).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainListAdd_NormalOrder)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Container {
    expose List[Integer] items;
    expose func init() { items = []; }
}

entity User {
    expose Container container;
    expose func init() {
        container = new Container();
        container.init();
    }
    expose func addItem(val: Integer) {
        container.items.add(val);
    }
}

func start() {
    var u = new User();
    u.init();
    u.addItem(42);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006_norm.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Multiple entity field chains with different collection types.
TEST(ZiaBugFixes, BugFE006_EntityFieldChainMultipleCollections)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Manager {
    expose DataStore store;
    expose func init() {
        store = new DataStore();
        store.init();
    }
    expose func addValue(v: Integer) {
        store.values.add(v);
    }
    expose func addName(n: String) {
        store.names.add(n);
    }
}

entity DataStore {
    expose List[Integer] values;
    expose List[String] names;
    expose func init() {
        values = [];
        names = [];
    }
}

func start() {
    var m = new Manager();
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

/// @brief Entity field chain accessing an entity-typed field (not just List).
TEST(ZiaBugFixes, BugFE006_EntityFieldChainEntityField_ForwardRef)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Outer {
    expose Middle mid;
    expose func init() {
        mid = new Middle();
        mid.init();
    }
    expose func getInnerVal() -> Integer {
        return mid.inner.value;
    }
}

entity Middle {
    expose Inner inner;
    expose func init() {
        inner = new Inner();
        inner.value = 99;
    }
}

entity Inner {
    expose Integer value;
}

func start() {
    var o = new Outer();
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

/// @brief Test that entity methods can reference `final` constants defined later
/// in the same file. This was a bug where the single-pass lowering processed
/// entity methods before later `final` declarations, causing them to resolve to 0.
TEST(ZiaBugFixes, FinalConstantForwardReference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Config {
    expose Integer val;
    expose func init() {
        val = DEFAULT_SIZE;
    }
}

final DEFAULT_SIZE = 42;

func start() {
    var c = new Config();
    c.init();
    Viper.Terminal.SayInt(c.val);
}
)";

    CompilerInput input{.source = source, .path = "final_forward_ref.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Verify the constant was inlined correctly by checking the IL output
    // The entity method should use const 42, not 0
    if (result.succeeded())
    {
        auto &mod = result.module;
        bool found42 = false;
        for (auto &fn : mod.functions)
        {
            for (auto &bb : fn.blocks)
            {
                for (auto &instr : bb.instructions)
                {
                    for (auto &op : instr.operands)
                    {
                        if (op.kind == il::core::Value::Kind::ConstInt && op.i64 == 42)
                        {
                            found42 = true;
                        }
                    }
                }
            }
        }
        EXPECT_TRUE(found42);
    }
}

/// @brief Test that multiple finals defined after an entity all resolve correctly.
TEST(ZiaBugFixes, MultipleFinalConstantsForwardReference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity MathHelper {
    expose func getSum() -> Integer {
        return VAL_A + VAL_B + VAL_C;
    }
}

final VAL_A = 10;
final VAL_B = 20;
final VAL_C = 30;

func start() {
    var h = new MathHelper();
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
TEST(ZiaBugFixes, BugFE008_ChainedRuntimeMethodCalls)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections;

func start() {
    var data: Bytes = Bytes.FromStr("hello world");
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
TEST(ZiaBugFixes, BugFE008_MultipleChainedCalls)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Collections;

func start() {
    var data: Bytes = Bytes.FromStr("hello world!");
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

//===----------------------------------------------------------------------===//
// BUG-FE-009: List[Boolean].get(i) type mismatch in boolean expressions
//===----------------------------------------------------------------------===//

/// @brief List[Boolean].get(i) should be usable in if-conditions.
/// Previously, emitUnbox for I1 declared the call return type as I1 but
/// the runtime function rt_unbox_i1 actually returns i64, causing a type
/// mismatch in the generated IL.
TEST(ZiaBugFixes, BugFE009_ListBooleanGetInCondition)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var flags: List[Boolean] = [true, false, true];
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
TEST(ZiaBugFixes, BugFE009_ListBooleanGetInLogicalExpr)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var flags: List[Boolean] = [true, true, false];
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
TEST(ZiaBugFixes, BugFE010_CrossClassPtrMethodFallback)
{
    SourceManager sm;
    // We test with Bytes methods called on a Ptr-typed variable.
    // The key is that the method should resolve via cross-class fallback.
    const std::string source = R"(
module Test;

bind Viper.Collections;

func makeData() -> Bytes {
    return Bytes.FromStr("test");
}

func start() {
    var data = makeData();
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
                               const std::string &contents)
{
    fs::create_directories(dir);
    fs::path path = dir / name;
    std::ofstream out(path);
    out << contents;
    return path;
}

/// @brief A `final` constant with a non-literal initializer (e.g., a BinaryExpr
/// like `0 - 2147483647`) must be constant-folded so that the resulting IL uses
/// the correct value rather than constInt(0).
TEST(ZiaBugFixes, BugFE011_NonLiteralFinalFoldsToCorrectValue)
{
    SourceManager sm;
    // `final SENTINEL = 0 - 2147483647` is a BinaryExpr, not an IntLiteralExpr.
    // Before the fix, this resolved to constInt(0) everywhere it was referenced.
    const std::string source = R"(
module Test;

final SENTINEL = 0 - 2147483647;
final MASK = 255 & 15;

func start() {
    var s: Integer = SENTINEL;
    var m: Integer = MASK;
    Viper.Terminal.SayInt(s);
    Viper.Terminal.SayInt(m);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe011_nonliteral.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    if (result.succeeded())
    {
        // The IL must contain -2147483647 (= 0 - 2147483647) and 15 (= 255 & 15).
        // Before the fix, both would appear as 0.
        bool foundSentinel = false;
        bool foundMask = false;
        for (auto &fn : result.module.functions)
        {
            for (auto &bb : fn.blocks)
            {
                for (auto &instr : bb.instructions)
                {
                    for (auto &op : instr.operands)
                    {
                        if (op.kind == il::core::Value::Kind::ConstInt)
                        {
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
TEST(ZiaBugFixes, BugFE011_CrossModuleNonLiteralFinalConstant)
{
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

func start() {
    var x: Integer = SENTINEL;
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

    if (result.succeeded())
    {
        bool foundSentinel = false;
        for (auto &fn : result.module.functions)
        {
            for (auto &bb : fn.blocks)
            {
                for (auto &instr : bb.instructions)
                {
                    for (auto &op : instr.operands)
                    {
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

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
