//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia type system (struct types, class types).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Test that value types parse correctly.
TEST(ZiaTypes, ValueTypeDeclaration) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    Integer x;
    Integer y;
}

func start() {}
)";
    CompilerInput input{.source = source, .path = "value.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ValueTypeDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that class types with new keyword work correctly.
TEST(ZiaTypes, EntityTypeWithNew) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Person {
    expose Integer age;
    expose Integer score;

    expose func init(a: Integer, s: Integer) {        age = a;
        score = s;
    }

    expose func getAge() -> Integer {        return age;
    }
}

func start() {    var p: Person = new Person(30, 100);
    var age: Integer = p.age;
    var method_age: Integer = p.getAge();
    Zanna.Terminal.SayInt(age);
    Zanna.Terminal.SayInt(method_age);
}
)";
    CompilerInput input{.source = source, .path = "class.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for EntityTypeWithNew:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundRtObjNew = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "rt_obj_new_i64")
                        foundRtObjNew = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundRtObjNew);
}

/// @brief Test Bug #16 fix: Entity type as function parameter compiles correctly.
/// Previously caused an infinite loop in the parser.
TEST(ZiaTypes, EntityAsParameter) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Frog {
    expose Integer x;
}

func useFrog(f: Frog) {    Zanna.Terminal.SayInt(f.x);
}

func start() {    var f = new Frog();
    f.x = 42;
    useFrog(f);
}
)";
    CompilerInput input{.source = source, .path = "entity_param.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for EntityAsParameter:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the useFrog function exists and takes a parameter
    bool foundUseFrogFunc = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "Test.useFrog" || fn.name == "useFrog") {
            foundUseFrogFunc = true;
            EXPECT_EQ(fn.params.size(), 1u);
            break;
        }
    }
    EXPECT_TRUE(foundUseFrogFunc);
}

/// @brief Bug #20: Parameter name 'value' should be allowed (contextual keyword).
TEST(ZiaTypes, ValueAsParameterName) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Board {
    List[Integer] items;

    expose func init() {        items = [];
        items.add(0);
    }

    expose func doSet(idx: Integer, value: Integer) {        items.set(idx, value);
    }
}

func start() {    var b: Board = new Board();
    b.init();
    b.doSet(0, 42);
}
)";
    CompilerInput input{.source = source, .path = "value_param.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded()); // Bug #20: 'value' should be allowed as parameter name
}

/// @brief Bug #30: Boolean fields in entities should be properly aligned.
/// Ensures Boolean fields don't cause misaligned store errors at runtime.
TEST(ZiaTypes, BooleanFieldAlignment) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Game {
    expose Integer score;
    expose Boolean running;
    expose Boolean paused;
    expose Integer level;

    expose func init() {        score = 0;
        running = true;
        paused = false;
        level = 1;
    }

    expose func isRunning() -> Boolean {        return running;
    }
}

func start() {    var g: Game = new Game();
    g.init();
    var r: Boolean = g.isRunning();
}
)";
    CompilerInput input{.source = source, .path = "boolfields.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for BooleanFieldAlignment:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #30: Boolean fields should compile without errors
}

TEST(ZiaTypes, GenericInterfaceAndMethodDeclarationsResolveTypeParams) {
    SourceManager sm;
    const std::string source = R"(
module Test;

interface Boxable[T] {
    func get() -> T;
}

func acceptIntBox(value: Boxable[Integer]) {}

class IdentityBox {
    expose func id[T](value: T) -> T {
        return value;
    }
}

func start() {
    var box = new IdentityBox();
    var value: Integer = box.id[Integer](42);
    Zanna.Terminal.SayInt(value);
}
)";
    CompilerInput input{.source = source, .path = "generic_type_param_members.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for GenericInterfaceAndMethodDeclarationsResolveTypeParams:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypes, CompatibilityTypeAliasesAndBytes) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var count: int = 1;
    var ok: bool = true;
    var ratio: double = 1.5;
    var bytes: Bytes;
    Zanna.Terminal.SayInt(count);
    Zanna.Terminal.SayBool(ok);
}
)";
    CompilerInput input{.source = source, .path = "type_aliases_bytes.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypes, QualifiedGenericConstraintsParse) {
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Contracts {
    interface Named {
        func name() -> String;
    }
}

func identityName[T: Contracts.Named](value: T) -> String {
    return "";
}

func start() {}
)";
    CompilerInput input{.source = source, .path = "qualified_generic_constraint.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for QualifiedGenericConstraintsParse:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
