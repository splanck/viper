//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang namespace feature.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

/// @brief Test that a simple namespace declaration compiles.
TEST(ViperLangNamespaces, BasicNamespaceDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace MyLib {
    func helper() -> Integer {
        return 42;
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for BasicNamespaceDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the namespaced function exists with qualified name
    bool hasQualifiedFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("MyLib") != std::string::npos &&
            fn.name.find("helper") != std::string::npos)
        {
            hasQualifiedFunc = true;
            break;
        }
    }
    EXPECT_TRUE(hasQualifiedFunc);
}

/// @brief Test nested namespace declaration.
TEST(ViperLangNamespaces, NestedNamespace)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Outer {
    namespace Inner {
        func nested() -> Integer {
            return 100;
        }
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for NestedNamespace:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the nested function has the full qualified name
    bool hasNestedFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("Outer") != std::string::npos &&
            fn.name.find("Inner") != std::string::npos &&
            fn.name.find("nested") != std::string::npos)
        {
            hasNestedFunc = true;
            break;
        }
    }
    EXPECT_TRUE(hasNestedFunc);
}

/// @brief Test dotted namespace name (MyLib.Internal).
TEST(ViperLangNamespaces, DottedNamespaceName)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace MyLib.Internal {
    func secret() -> String {
        return "hidden";
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for DottedNamespaceName:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the function has the dotted qualified name
    bool hasDottedFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("MyLib") != std::string::npos &&
            fn.name.find("Internal") != std::string::npos &&
            fn.name.find("secret") != std::string::npos)
        {
            hasDottedFunc = true;
            break;
        }
    }
    EXPECT_TRUE(hasDottedFunc);
}

/// @brief Test entity inside namespace.
TEST(ViperLangNamespaces, EntityInNamespace)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace MyLib {
    entity Parser {
        Integer value;

        func init() {
            value = 0;
        }

        func getValue() -> Integer {
            return value;
        }
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityInNamespace:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that entity methods have qualified names
    bool hasQualifiedMethod = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("MyLib") != std::string::npos &&
            fn.name.find("Parser") != std::string::npos)
        {
            hasQualifiedMethod = true;
            break;
        }
    }
    EXPECT_TRUE(hasQualifiedMethod);
}

/// @brief Test global variable inside namespace.
TEST(ViperLangNamespaces, GlobalVarInNamespace)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Config {
    final VERSION = 42;
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for GlobalVarInNamespace:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test value type inside namespace.
TEST(ViperLangNamespaces, ValueTypeInNamespace)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Geometry {
    value Point {
        Integer x;
        Integer y;
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ValueTypeInNamespace:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test interface inside namespace.
TEST(ViperLangNamespaces, InterfaceInNamespace)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace IO {
    interface Readable {
        func read() -> String;
    }
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for InterfaceInNamespace:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test calling a function from a namespace.
TEST(ViperLangNamespaces, CallNamespacedFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Math {
    func add(a: Integer, b: Integer) -> Integer {
        return a + b;
    }

    func multiply(a: Integer, b: Integer) -> Integer {
        return a * b;
    }
}

func start() {
    var sum = Math.add(3, 4);
    var product = Math.multiply(5, 6);
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for CallNamespacedFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify both namespaced functions exist
    bool hasAdd = false;
    bool hasMultiply = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("Math") != std::string::npos && fn.name.find("add") != std::string::npos)
        {
            hasAdd = true;
        }
        if (fn.name.find("Math") != std::string::npos && fn.name.find("multiply") != std::string::npos)
        {
            hasMultiply = true;
        }
    }
    EXPECT_TRUE(hasAdd);
    EXPECT_TRUE(hasMultiply);
}

/// @brief Test calling a nested namespaced function.
TEST(ViperLangNamespaces, CallNestedNamespacedFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

namespace Outer {
    namespace Inner {
        func getValue() -> Integer {
            return 42;
        }
    }
}

func start() {
    var x = Outer.Inner.getValue();
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for CallNestedNamespacedFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
