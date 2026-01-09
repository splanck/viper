//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_method_call_arg_single_eval.cpp
// Purpose: Verify Bug #021 fix - method call arguments with side effects are
//          evaluated only once, not twice when type coercion is applied.
// Key invariants: Function call arguments should execute side effects once.
// Ownership/Lifetime: Test file.
// Links: docs/bugs/sqldb_bugs.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

/// @brief Test that method call arguments are lowered only once.
///
/// Bug #021: When a method parameter expects a type different from the argument,
/// the lowerer was calling lowerExpr twice - once to get the value and again
/// for coercion. This caused side effects (function calls) to execute twice.
TEST(BasicMethodCallArgEval, FunctionArgEvaluatedOnce)
{
    // This test verifies that when passing a function call as an argument
    // to a method, the function is only called once even when type coercion
    // is required.
    const std::string src = R"(
10 DIM counter AS INTEGER
20 counter = 0

30 FUNCTION IncrementAndReturn() AS DOUBLE
40   counter = counter + 1
50   RETURN CDbl(counter)
60 END FUNCTION

70 CLASS Receiver
80   SUB TakeDouble(val AS DOUBLE)
90     PRINT val
100  END SUB
110 END CLASS

120 DIM r AS Receiver
130 r = NEW Receiver()
140 r.TakeDouble(IncrementAndReturn())
150 PRINT counter
160 END
)";

    SourceManager sm;
    BasicCompilerInput input{src, "arg_eval_test.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;

    // Count how many times IncrementAndReturn is called in main
    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : mod.functions)
    {
        if (fn.name == "main" || fn.name == "MAIN")
        {
            mainFn = &fn;
            break;
        }
    }
    ASSERT_NE(mainFn, nullptr);

    int callCount = 0;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call)
            {
                // Check if callee is IncrementAndReturn (case-insensitive)
                std::string callee = in.callee;
                std::transform(callee.begin(), callee.end(), callee.begin(), ::tolower);
                if (callee.find("incrementandreturn") != std::string::npos)
                {
                    callCount++;
                }
            }
        }
    }

    // Bug #021: Before the fix, this would be 2 (function called twice).
    // After the fix, it should be exactly 1.
    EXPECT_EQ(callCount, 1);
}

/// @brief Test that method call arguments with integer-to-double coercion work.
TEST(BasicMethodCallArgEval, IntToDoubleCoercion)
{
    const std::string src = R"(
10 DIM callCount AS INTEGER
20 callCount = 0

30 FUNCTION GetValue() AS INTEGER
40   callCount = callCount + 1
50   RETURN 42
60 END FUNCTION

70 CLASS Calculator
80   SUB Process(x AS DOUBLE)
90     PRINT x
100  END SUB
110 END CLASS

120 DIM calc AS Calculator
130 calc = NEW Calculator()
140 calc.Process(GetValue())
150 PRINT callCount
160 END
)";

    SourceManager sm;
    BasicCompilerInput input{src, "int_to_double_test.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : mod.functions)
    {
        if (fn.name == "main" || fn.name == "MAIN")
        {
            mainFn = &fn;
            break;
        }
    }
    ASSERT_NE(mainFn, nullptr);

    int callCount = 0;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call)
            {
                std::string callee = in.callee;
                std::transform(callee.begin(), callee.end(), callee.begin(), ::tolower);
                if (callee.find("getvalue") != std::string::npos)
                {
                    callCount++;
                }
            }
        }
    }

    EXPECT_EQ(callCount, 1);
}

/// @brief Test multiple arguments with coercion.
TEST(BasicMethodCallArgEval, MultipleArgsWithCoercion)
{
    const std::string src = R"(
10 DIM callA AS INTEGER
20 DIM callB AS INTEGER
30 callA = 0
40 callB = 0

50 FUNCTION GetA() AS INTEGER
60   callA = callA + 1
70   RETURN 10
80 END FUNCTION

90 FUNCTION GetB() AS INTEGER
100  callB = callB + 1
110  RETURN 20
120 END FUNCTION

130 CLASS Adder
140   SUB Add(a AS DOUBLE, b AS DOUBLE)
150     PRINT a + b
160   END SUB
170 END CLASS

180 DIM adder AS Adder
190 adder = NEW Adder()
200 adder.Add(GetA(), GetB())
210 PRINT callA; callB
220 END
)";

    SourceManager sm;
    BasicCompilerInput input{src, "multi_args_test.bas"};
    BasicCompilerOptions opts{};
    auto result = compileBasic(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    const il::core::Module &mod = result.module;

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : mod.functions)
    {
        if (fn.name == "main" || fn.name == "MAIN")
        {
            mainFn = &fn;
            break;
        }
    }
    ASSERT_NE(mainFn, nullptr);

    int callCountA = 0;
    int callCountB = 0;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &in : bb.instructions)
        {
            if (in.op == il::core::Opcode::Call)
            {
                std::string callee = in.callee;
                std::transform(callee.begin(), callee.end(), callee.begin(), ::tolower);
                if (callee.find("geta") != std::string::npos)
                    callCountA++;
                if (callee.find("getb") != std::string::npos)
                    callCountB++;
            }
        }
    }

    EXPECT_EQ(callCountA, 1);
    EXPECT_EQ(callCountB, 1);
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
