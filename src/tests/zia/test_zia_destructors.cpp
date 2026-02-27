//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia destructor (deinit) declarations.
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

/// @brief Check if a function returns void.
static bool returnsVoid(const il::core::Module &mod, const std::string &fnName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
            return fn.retType.kind == il::core::Type::Kind::Void;
    }
    return false;
}

/// @brief Check if a function calls a specific callee.
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

// ============================================================================
// Destructor tests
// ============================================================================

/// @brief Test that a basic deinit block compiles and produces __dtor function.
TEST(ZiaDestructors, BasicDeinit)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Connection {
    expose String host;

    deinit {
        var x = 0;
    }
}

func start() {
    var c = new Connection();
}
)";

    CompilerInput input{.source = source, .path = "test_dtor_basic.zia"};
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

    // Should have synthesized __dtor function
    EXPECT_TRUE(hasFunction(result.module, "Connection.__dtor"));
    // Destructor should have self parameter
    EXPECT_TRUE(hasSelfParam(result.module, "Connection.__dtor"));
    // Destructor should return void
    EXPECT_TRUE(returnsVoid(result.module, "Connection.__dtor"));
}

/// @brief Test that destructor emits field release calls for String fields.
TEST(ZiaDestructors, ReleasesStringFields)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Logger {
    expose String name;
    expose String path;

    deinit {
        var x = 0;
    }
}

func start() {
    var l = new Logger();
}
)";

    CompilerInput input{.source = source, .path = "test_dtor_release.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "Logger.__dtor"));
    // Should call rt_str_release_maybe for String fields
    EXPECT_TRUE(hasCallee(result.module, "Logger.__dtor", "rt_str_release_maybe"));
}

/// @brief Test that entity without deinit does NOT produce __dtor function.
TEST(ZiaDestructors, NoDeinitNoDtor)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var p = new Point();
}
)";

    CompilerInput input{.source = source, .path = "test_no_dtor.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    // No deinit -> no __dtor function
    EXPECT_FALSE(hasFunction(result.module, "Point.__dtor"));
}

/// @brief Test destructor with user code that accesses self fields.
TEST(ZiaDestructors, DeinitAccessesSelf)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Resource {
    expose Integer refCount;

    deinit {
        var count = self.refCount;
    }
}

func start() {
    var r = new Resource();
}
)";

    CompilerInput input{.source = source, .path = "test_dtor_self.zia"};
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
    EXPECT_TRUE(hasFunction(result.module, "Resource.__dtor"));
    EXPECT_TRUE(hasSelfParam(result.module, "Resource.__dtor"));
}

/// @brief Test that destructor coexists with constructor and methods.
TEST(ZiaDestructors, DeinitWithCtorAndMethods)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Handle {
    expose Integer id;

    func getId() -> Integer {
        return self.id;
    }

    deinit {
        var x = self.id;
    }
}

func start() {
    var h = new Handle();
}
)";

    CompilerInput input{.source = source, .path = "test_dtor_coexist.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    // Both method and destructor should exist
    EXPECT_TRUE(hasFunction(result.module, "Handle.getId"));
    EXPECT_TRUE(hasFunction(result.module, "Handle.__dtor"));
}

} // anonymous namespace

int main()
{
    return viper_test::run_all_tests();
}
