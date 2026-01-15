//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia type system (value types, entity types).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Test that value types parse correctly.
TEST(ZiaTypes, ValueTypeDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    Integer x;
    Integer y;
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "value.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ValueTypeDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that entity types with new keyword work correctly.
TEST(ZiaTypes, EntityTypeWithNew)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
    expose Integer score;

    expose func getAge() -> Integer {
        return age;
    }
}

func start() {
    Person p = new Person(30, 100);
    Integer age = p.age;
    Integer method_age = p.getAge();
    Viper.Terminal.SayInt(age);
    Viper.Terminal.SayInt(method_age);
}
)";
    CompilerInput input{.source = source, .path = "entity.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityTypeWithNew:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundRtObjNew = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
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
TEST(ZiaTypes, EntityAsParameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Frog {
    expose Integer x;
}

func useFrog(Frog f) {
    Viper.Terminal.SayInt(f.x);
}

func start() {
    var f = new Frog();
    f.x = 42;
    useFrog(f);
}
)";
    CompilerInput input{.source = source, .path = "entity_param.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityAsParameter:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the useFrog function exists and takes a parameter
    bool foundUseFrogFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "Test.useFrog" || fn.name == "useFrog")
        {
            foundUseFrogFunc = true;
            EXPECT_EQ(fn.params.size(), 1u);
            break;
        }
    }
    EXPECT_TRUE(foundUseFrogFunc);
}

/// @brief Bug #20: Parameter name 'value' should be allowed (contextual keyword).
TEST(ZiaTypes, ValueAsParameterName)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Board {
    List[Integer] items;

    expose func init() {
        items = [];
        items.add(0);
    }

    expose func doSet(Integer idx, Integer value) {
        items.set(idx, value);
    }
}

func start() {
    Board b = new Board();
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
TEST(ZiaTypes, BooleanFieldAlignment)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Game {
    expose Integer score;
    expose Boolean running;
    expose Boolean paused;
    expose Integer level;

    expose func init() {
        score = 0;
        running = true;
        paused = false;
        level = 1;
    }

    expose func isRunning() -> Boolean {
        return running;
    }
}

func start() {
    Game g = new Game();
    g.init();
    Boolean r = g.isRunning();
}
)";
    CompilerInput input{.source = source, .path = "boolfields.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for BooleanFieldAlignment:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #30: Boolean fields should compile without errors
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
