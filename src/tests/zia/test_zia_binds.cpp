//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia bind resolution.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
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

namespace fs = std::filesystem;

fs::path writeFile(const fs::path &dir, const std::string &name, const std::string &contents)
{
    fs::create_directories(dir);
    fs::path path = dir / name;
    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

TEST(ZiaBinds, BindStringLiteralWithExtension)
{
    const fs::path tempRoot = fs::temp_directory_path() / "zia_bind_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "bind_ok";

    const fs::path libPath = writeFile(dir,
                                       "lib.zia",
                                       R"(
module Lib;

func greet() {
    Viper.Terminal.Say("hi");
}
)");

    const std::string mainSource = R"(
module Main;
bind "lib.zia";

func start() {
    greet();
}
)";
    const fs::path mainPath = writeFile(dir, "main.zia", mainSource);
    const std::string mainPathStr = mainPath.string();

    SourceManager sm;
    CompilerInput input{.source = mainSource, .path = mainPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for BindStringLiteralWithExtension:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    bool hasGreet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "greet")
            hasGreet = true;
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasGreet);

    (void)libPath;
}

TEST(ZiaBinds, MissingBindReportsAtBindSite)
{
    const fs::path tempRoot = fs::temp_directory_path() / "zia_bind_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "missing_bind";

    const std::string mainSource = R"(
module Main;
bind "missing.zia";

func start() {
}
)";
    const fs::path mainPath = writeFile(dir, "main.zia", mainSource);
    const std::string mainPathStr = mainPath.string();

    SourceManager sm;
    CompilerInput input{.source = mainSource, .path = mainPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());

    bool foundError = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("Failed to open imported file") == std::string::npos)
            continue;
        foundError = true;
        EXPECT_EQ(d.code, "V1000");
        EXPECT_EQ(d.loc.file_id, result.fileId);
    }
    EXPECT_TRUE(foundError);
}

TEST(ZiaBinds, CircularBindDetected)
{
    const fs::path tempRoot = fs::temp_directory_path() / "zia_bind_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "cycle";

    const std::string aSource = R"(
module A;
bind "b.zia";

func a() {
}

func start() {
    a();
}
)";
    const fs::path aPath = writeFile(dir, "a.zia", aSource);
    const std::string aPathStr = aPath.string();

    const std::string bSource = R"(
module B;
bind "a.zia";

func b() {
}
)";
    const fs::path bPath = writeFile(dir, "b.zia", bSource);

    SourceManager sm;
    CompilerInput input{.source = aSource, .path = aPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_FALSE(result.succeeded());

    const uint32_t bFileId = sm.addFile(bPath.string());

    bool foundCycle = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("Circular import detected") == std::string::npos)
            continue;
        foundCycle = true;
        EXPECT_EQ(d.code, "V1000");
        EXPECT_EQ(d.loc.file_id, bFileId);
    }
    EXPECT_TRUE(foundCycle);
}

/// @brief Test that transitive binds maintain correct declaration order (Bug #26).
/// When main binds both inner and outer, where outer also binds inner,
/// the entities must be lowered in dependency order (Inner before Outer).
TEST(ZiaBinds, TransitiveBindDeclarationOrder)
{
    const fs::path tempRoot = fs::temp_directory_path() / "zia_bind_tests" /
                              std::to_string(static_cast<unsigned long long>(::getpid()));
    const fs::path dir = tempRoot / "transitive_order";

    // Inner entity with a method
    const fs::path innerPath = writeFile(dir, "inner.zia", R"(
module Inner;

entity Inner {
    expose Integer myValue;

    expose func init(Integer v) {
        myValue = v;
    }

    expose func getValue() -> Integer {
        return myValue;
    }
}
)");

    // Outer entity that has Inner field and calls its method
    const fs::path outerPath = writeFile(dir, "outer.zia", R"(
module Outer;

bind "./inner";

entity Outer {
    expose Inner inner;

    expose func test() -> Integer {
        return inner.getValue();
    }
}
)");

    // Main binds both inner AND outer (outer also binds inner)
    const std::string mainSource = R"(
module Main;

bind "./inner";
bind "./outer";

func start() {
    Outer o = new Outer();
    o.inner = new Inner(42);
    Integer result = o.test();
    Viper.Terminal.SayInt(result);
}
)";
    const fs::path mainPath = writeFile(dir, "main.zia", mainSource);
    const std::string mainPathStr = mainPath.string();

    SourceManager sm;
    CompilerInput input{.source = mainSource, .path = mainPathStr};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for TransitiveBindDeclarationOrder:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }
    EXPECT_TRUE(result.succeeded());

    // Verify Outer.test calls Inner.getValue directly (not via lambda/closure)
    bool foundOuterTest = false;
    bool foundDirectCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "Outer.test")
        {
            foundOuterTest = true;
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call)
                    {
                        // Check if callee is Inner.getValue (direct call)
                        if (instr.callee == "Inner.getValue")
                        {
                            foundDirectCall = true;
                        }
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundOuterTest);
    EXPECT_TRUE(foundDirectCall);

    (void)innerPath;
    (void)outerPath;
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
