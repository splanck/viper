//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Regression tests for Zia language hardening fixes.
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

CompilerResult compileSource(const std::string &source, const std::string &path) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    return compile(input, {}, sm);
}

bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &diag : result.diagnostics.diagnostics()) {
        if (diag.severity == Severity::Error && diag.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool hasCall(const il::core::Module &module, const std::string &callee) {
    for (const auto &fn : module.functions) {
        for (const auto &block : fn.blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

} // namespace

TEST(ZiaLanguageHardening, BlockExpressionsReturnTrailingValue) {
    auto result = compileSource(R"(
module Test;

func start() {
    var x: Integer = {
        var a = 10;
        var b = 20;
        a + b
    };
    var add = (n: Integer) => {
        var one = 1;
        n + one
    };
    Zanna.Terminal.SayInt(add(x));
}
)",
                                "block_exprs.zia");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaLanguageHardening, MatchExpressionRejectsIncompatibleArmTypes) {
    auto result = compileSource(R"(
module Test;

func start() {
    var x = match 1 {
        1 => "one"
        _ => 0
    };
}
)",
                                "match_arm_types.zia");

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Incompatible match arm type"));
}

TEST(ZiaLanguageHardening, IsAndAsProduceBooleanAndCheckedUnwraps) {
    auto result = compileSource(R"(
module Test;

interface Named {
    func name() -> String;
}

class Animal {
    expose String label;
}

class Dog extends Animal implements Named {
    expose String breed;
    expose func name() -> String {
        return self.label;
    }
}

func start() {
    var dog = new Dog();
    var animal: Animal = dog;
    var maybe: Integer? = 42;
    var value: Integer = maybe as Integer;
    var a: Boolean = dog is Animal;
    var b: Boolean = dog is Named;
    var c: Boolean = value is Integer;
    Zanna.Terminal.SayInt((a && b && c) ? value : 0);
}
)",
                                "is_as_hardening.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "rt_type_implements"));
}

TEST(ZiaLanguageHardening, ForwardTypeAndAliasReferencesCompile) {
    auto result = compileSource(R"(
module Test;

type Pet = Animal;

interface Named {
    func name() -> String;
}

func adopt(dog: Dog) -> Pet {
    return dog;
}

func identify(dog: Dog) -> Named {
    return dog;
}

var kennel: Pet = new Dog();

class Dog extends Animal implements Named {
    expose Integer age;
    expose func name() -> String {
        return "dog";
    }
}

class Animal {
    expose Integer id;
}

func start() {
    var pet: Pet = adopt(new Dog());
    var named: Named = identify(new Dog());
    Zanna.Terminal.SayBool(pet is Animal);
    Zanna.Terminal.Say(named.name());
}
)",
                                "forward_type_refs.zia");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaLanguageHardening, EmptySetAndContextualMatchIdentifierCompile) {
    auto result = compileSource(R"(
module Test;

func start() {
    var s: Set[Integer] = {};
    var explicitSet = set {1, 2, 3};
    var emptyMap: Map[String, Integer] = map {};
    var explicitMap = map {"one": 1, "two": 2};
    var match: (Integer) -> Integer = (x: Integer) => x + 1;
    Zanna.Terminal.SayInt(match(2) + explicitSet.Count + emptyMap.Count + explicitMap.Count);
}
)",
                                "empty_set_match_identifier.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "Zanna.Collections.Set.New"));
    EXPECT_TRUE(hasCall(result.module, "Zanna.Collections.Map.New"));
}

TEST(ZiaLanguageHardening, RuntimeCollectionForInCompiles) {
    auto result = compileSource(R"(
module Test;

func start() {
    var q: Queue[String] = new Queue[String]();
    q.push("a");
    for item in q {
        Zanna.Terminal.Say(item);
    }

    var stack: Stack[Integer] = new Stack[Integer]();
    stack.push(1);
    for index, value in stack {
        Zanna.Terminal.SayInt(index + value);
    }
}
)",
                                "runtime_collection_forin.zia");

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "Zanna.Collections.Queue.ToSeq"));
    EXPECT_TRUE(hasCall(result.module, "Zanna.Collections.Stack.ToSeq"));
}

TEST(ZiaLanguageHardening, ModuleHeaderIsOptional) {
    auto result = compileSource(R"(
func start() {
    Zanna.Terminal.SayInt(1);
}
)",
                                "implicit_module.zia");

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaLanguageHardening, DuplicateLocalVariableRejected) {
    auto result = compileSource(R"(
module Test;

func start() {
    var x = 1;
    var x = 2;
    Zanna.Terminal.SayInt(x);
}
)",
                                "duplicate_local.zia");

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Duplicate definition of 'x'"));
}

TEST(ZiaLanguageHardening, AnyStringConcatenationRequiresExplicitConversion) {
    auto result = compileSource(R"(
module Test;

func anyValue() -> Any {
    return 1;
}

func start() {
    var text = "value: " + anyValue();
    Zanna.Terminal.Say(text);
}
)",
                                "any_string_concat.zia");

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Cannot concatenate String with Any"));
}

int main() {
    return zanna_test::run_all_tests();
}
