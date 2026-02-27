//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia properties (get/set) and static members.
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

// ============================================================================
// Helpers
// ============================================================================

static bool hasFunction(const il::core::Module &mod, const std::string &fnName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
            return true;
    }
    return false;
}

static bool hasCallee(const il::core::Module &mod, const std::string &fnName,
                      const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                        return true;
                }
            }
        }
    }
    return false;
}

/// @brief Check if a function has a "self" parameter.
static bool hasSelfParam(const il::core::Module &mod, const std::string &fnName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &param : fn.params)
            {
                if (param.name == "self")
                    return true;
            }
            return false;
        }
    }
    return false;
}

/// @brief Check if a global variable exists in the module.
static bool hasGlobal(const il::core::Module &mod, const std::string &globalName)
{
    for (const auto &g : mod.globals)
    {
        if (g.name == globalName)
            return true;
    }
    return false;
}

// ============================================================================
// Property tests
// ============================================================================

/// @brief Test that a property with a getter synthesizes get_PropertyName.
TEST(ZiaProperties, GetterSynthesized)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Circle {
    expose Number radius;

    property area: Number {
        get {
            return self.radius * self.radius;
        }
    }
}

func start() {
    var c = new Circle();
}
)";

    CompilerInput input{.source = source, .path = "test_prop_get.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());

    // Should have synthesized get_area method
    EXPECT_TRUE(hasFunction(result.module, "Circle.get_area"));
    // Getter should have self parameter
    EXPECT_TRUE(hasSelfParam(result.module, "Circle.get_area"));
}

/// @brief Test property with getter and setter.
TEST(ZiaProperties, GetterAndSetter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Temperature {
    expose Number celsius;

    property fahrenheit: Number {
        get {
            return self.celsius * 1.8 + 32.0;
        }
        set(f) {
            self.celsius = (f - 32.0) / 1.8;
        }
    }
}

func start() {
    var t = new Temperature();
}
)";

    CompilerInput input{.source = source, .path = "test_prop_getset.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());

    // Should have synthesized both get_ and set_ methods
    EXPECT_TRUE(hasFunction(result.module, "Temperature.get_fahrenheit"));
    EXPECT_TRUE(hasFunction(result.module, "Temperature.set_fahrenheit"));
}

// ============================================================================
// Static member tests
// ============================================================================

/// @brief Test that static methods don't have self parameter.
TEST(ZiaStatic, StaticMethodNoSelf)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Counter {
    expose Integer value;

    static func create() -> Integer {
        return 42;
    }
}

func start() {
    var c = new Counter();
}
)";

    CompilerInput input{.source = source, .path = "test_static_method.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());

    // Static method should exist
    EXPECT_TRUE(hasFunction(result.module, "Counter.create"));
    // Static method should NOT have self parameter
    EXPECT_FALSE(hasSelfParam(result.module, "Counter.create"));
}

/// @brief Test that static fields are excluded from instance layout.
/// @details Static fields don't contribute to the entity's instance size,
/// and are stored at module level. We verify the entity compiles successfully
/// with a static field declaration.
TEST(ZiaStatic, StaticFieldCompiles)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Config {
    expose Integer value;
    static Integer count = 0;
}

func start() {
    var c = new Config();
}
)";

    CompilerInput input{.source = source, .path = "test_static_field.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
}

/// @brief Test that non-static methods still have self.
TEST(ZiaStatic, NonStaticMethodHasSelf)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Box {
    expose Integer width;

    func getWidth() -> Integer {
        return self.width;
    }
}

func start() {
    var b = new Box();
}
)";

    CompilerInput input{.source = source, .path = "test_nonstatic.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // Non-static method should have self parameter
    EXPECT_TRUE(hasSelfParam(result.module, "Box.getWidth"));
}

} // anonymous namespace

int main()
{
    return viper_test::run_all_tests();
}
