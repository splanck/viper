//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/oop/TestOverloads.cpp
// Purpose: Exercise overload resolver ambiguity with method vs property same name.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::basic;

TEST(OOP_Overloads, AmbiguousMethodVsPropertyGetter)
{
    // Define both a parameterless method Foo and a PROPERTY Foo with GET.
    // Access `o.Foo` should be ambiguous for resolver.
    const char *src = R"BAS(
10 CLASS C
20   FUNCTION Foo() AS INTEGER
30     RETURN 1
40   END FUNCTION
50   PROPERTY Foo :INTEGER
60     GET
70       RETURN 2
80     END GET
90   END PROPERTY
100 END CLASS
110 DIM o
120 LET o = NEW C()
130 PRINT o.Foo
140 END
)BAS";
    il::support::SourceManager sm;
    BasicCompilerInput in{src, "ambig.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    EXPECT_FALSE(res.succeeded());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
