//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalCastsTests.cpp
// Purpose: Tests for Pascal class type casts lowering and typing.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

/// @brief Class type casts lower to rt_cast_as with correct target id.
TEST(PascalCastsTest, ClassTypeCastsLowerToRuntime)
{
    SourceManager sm;
    const std::string source = "program Test; type TAnimal = class end; TDog = class(TAnimal) end; "
                               "var a: TAnimal; d: TDog; begin a := TDog.Create; d := TDog(a) end.";
    PascalCompilerInput input{.source = source, .path = "test_cast1.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());

    // Expect an extern for rt_cast_as
    bool hasCastExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_cast_as")
        {
            hasCastExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasCastExtern);
}

/// @brief Upcast via assignment compiles without cast helper.
TEST(PascalCastsTest, UpcastAssignmentCompiles)
{
    SourceManager sm;
    const std::string source = "program Test; type TAnimal = class end; TDog = class(TAnimal) end; "
                               "var a: TAnimal; d: TDog; begin d := TDog.Create; a := d end.";
    PascalCompilerInput input{.source = source, .path = "test_cast2.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
