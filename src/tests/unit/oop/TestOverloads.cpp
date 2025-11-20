// File: src/tests/unit/oop/TestOverloads.cpp
// Purpose: Exercise overload resolver ambiguity with method vs property same name.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"

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

#ifndef GTEST_HAS_MAIN
int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
