//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/oop/TestProperties.cpp
// Purpose: Verify PROPERTY synthesis produces get_/set_ accessors for instance/static.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Module.hpp"
#include "tests/TestHarness.hpp"

using namespace il::frontends::basic;

static bool hasFunc(const il::core::Module &m, std::string_view n)
{
    for (const auto &f : m.functions)
        if (f.name == n)
            return true;
    return false;
}

TEST(OOP_Properties, SynthesizesGetSetInstanceAndStatic)
{
    const char *src = R"BAS(
10 CLASS P
20   PROPERTY Name AS STRING
30     GET
40       RETURN "x"
50     END GET
60     SET(v)
70     END SET
80   END PROPERTY
90   STATIC PROPERTY Count AS INTEGER
100    GET
110      RETURN 0
120    END GET
130  END PROPERTY
140 END CLASS
150 END
)BAS";
    il::support::SourceManager sm;
    BasicCompilerInput in{src, "props.bas"};
    BasicCompilerOptions opt{};
    auto res = compileBasic(in, opt, sm);
    ASSERT_TRUE(res.succeeded());
    const auto &m = res.module;
    EXPECT_TRUE(hasFunc(m, "P.get_Name"));
    EXPECT_TRUE(hasFunc(m, "P.set_Name"));
    EXPECT_TRUE(hasFunc(m, "P.get_Count"));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
