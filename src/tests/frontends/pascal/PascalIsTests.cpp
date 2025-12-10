//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalIsTests.cpp
// Purpose: Tests for Pascal 'is' type-check operator.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

TEST(PascalIsTest, ClassIsChecksCompileAndLower)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TAnimal = class end;
  TDog = class(TAnimal) end;
  TCat = class(TAnimal) end;
var
  a: TAnimal;
  d: TDog;
  c: TCat;
  b1, b2, b3: Boolean;
begin
  d := TDog.Create;
  c := TCat.Create;
  a := d;
  b1 := a is TAnimal;
  b2 := a is TDog;
  b3 := a is TCat
end.
)";
    PascalCompilerInput input{.source = source, .path = "test_is.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Expect an extern for rt_cast_as used by 'is'
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

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
