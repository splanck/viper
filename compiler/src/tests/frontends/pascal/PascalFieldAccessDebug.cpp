//===----------------------------------------------------------------------===//
// Debug test to print diagnostics for field/method access cases
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <iostream>
using namespace il::frontends::pascal;
using namespace il::support;

TEST(PascalFieldDebug, PrintDiagnostics)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
type
  TInner = class
  public
    Val: Integer;
    procedure IncVal;
  end;

  TOuter = class
  private
    Inner: TInner;
  public
    constructor Create;
    procedure Bump;
  end;

constructor TOuter.Create;
begin
  Inner := TInner.Create;
  Inner.Val := 1
end;

procedure TInner.IncVal;
begin
  Inc(Val)
end;

procedure TOuter.Bump;
var tmp: TInner;
begin
  Inner.IncVal;
  tmp := Inner;
  tmp.IncVal;
  self.Inner.IncVal
end;

begin
end.
)";
    PascalCompilerInput input{.source = source, .path = "debug.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    if (!result.succeeded())
    {
        result.diagnostics.printAll(std::cerr, &sm);
    }
    EXPECT_TRUE(result.succeeded());
}

int main()
{
    return viper_test::run_all_tests();
}
