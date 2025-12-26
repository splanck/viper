// Debug test to inspect qualified method parsing
#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "support/diagnostics.hpp"
#include "tests/TestHarness.hpp"
#include <iostream>
using namespace il::frontends::pascal;
using namespace il::support;

TEST(PascalParserDebug, QualifiedMethod)
{
    const std::string src = R"(
program P;
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
begin end;

procedure TInner.IncVal;
begin end;

procedure TOuter.Bump;
begin end;

begin end.
)";
    DiagnosticEngine diag;
    Lexer lex(src, 1, diag);
    Parser parser(lex, diag);
    auto prog = parser.parseProgram();
    ASSERT_TRUE(prog != nullptr);
    bool seenCtor = false, seenProc = false;
    for (auto &d : prog->decls)
    {
        if (d->kind == DeclKind::Constructor)
        {
            auto &cd = static_cast<ConstructorDecl &>(*d);
            if (cd.name == "Create")
            {
                seenCtor = true;
                std::cerr << "ctor className=" << cd.className << "\n";
                EXPECT_EQ(cd.className, std::string("TOuter"));
            }
        }
        if (d->kind == DeclKind::Procedure)
        {
            auto &pd = static_cast<ProcedureDecl &>(*d);
            if (pd.name == "Bump")
            {
                seenProc = true;
                std::cerr << "proc className=" << pd.className << "\n";
                EXPECT_EQ(pd.className, std::string("TOuter"));
            }
        }
    }
    EXPECT_TRUE(seenCtor);
    EXPECT_TRUE(seenProc);
}

int main()
{
    return viper_test::run_all_tests();
}
