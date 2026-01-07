//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalPropertyTests.cpp
// Purpose: Tests for Pascal class properties (parse, semantics, lowering).
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

TEST(PascalPropertyTest, ParseSemanticsLoweringBasic)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TPerson = class
  private
    FAge: Integer;
    function GetAge: Integer;
    procedure SetAge(Value: Integer);
  public
    property Age: Integer read GetAge write SetAge;
    property RawAge: Integer read FAge write FAge;
  end;

function TPerson.GetAge: Integer;
begin
  Result := FAge;
end;

procedure TPerson.SetAge(Value: Integer);
begin
  FAge := Value;
end;

var p: TPerson; x, y: Integer;
begin
  p := TPerson.Create;
  p.Age := 10;
  p.RawAge := 20;
  x := p.Age;
  y := p.RawAge
end.
)";

    PascalCompilerInput input{.source = source, .path = "prop_test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Scan for calls to TPerson.SetAge and TPerson.GetAge in main
    bool hasSetAgeCall = false;
    bool hasGetAgeCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name != "main")
            continue;
        for (const auto &bb : fn.blocks)
        {
            for (const auto &ins : bb.instructions)
            {
                if (ins.op == il::core::Opcode::Call)
                {
                    if (ins.callee == "TPerson.SetAge")
                        hasSetAgeCall = true;
                    if (ins.callee == "TPerson.GetAge")
                        hasGetAgeCall = true;
                }
            }
        }
    }

    EXPECT_TRUE(hasSetAgeCall);
    EXPECT_TRUE(hasGetAgeCall);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
