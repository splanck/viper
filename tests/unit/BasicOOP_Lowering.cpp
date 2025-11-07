// File: tests/unit/BasicOOP_Lowering.cpp
// Purpose: Ensure BASIC OOP lowering emits runtime helpers and mangled members.
// Key invariants: Lowering produces required object runtime externs and class
//                 member functions with OOP permanently enabled.
// Ownership/Lifetime: Test owns compilation inputs and inspects resulting module.
// Links: docs/codemap.md

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
constexpr std::string_view kLoweringSnippet = R"BASIC(
10 CLASS Klass
20   value AS INTEGER
30   SUB NEW()
40     LET value = 1
50   END SUB
60   SUB INC()
70     LET value = value + 1
80   END SUB
90   DESTRUCTOR
100    LET value = value
110  END DESTRUCTOR
120 END CLASS
130 DIM o
140 LET o = NEW Klass()
150 PRINT o.INC()
160 DELETE o
170 END
)BASIC";

[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(externs.begin(),
                       externs.end(),
                       [&](const il::core::Extern &ext) { return ext.name == name; });
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        const unsigned char lc = static_cast<unsigned char>(lhs[i]);
        const unsigned char rc = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(lc) != std::tolower(rc))
            return false;
    }
    return true;
}

[[nodiscard]] bool hasFunction(const il::core::Module &module, std::string_view name)
{
    const auto &functions = module.functions;
    return std::any_of(functions.begin(),
                       functions.end(),
                       [&](const il::core::Function &fn)
                       { return equalsIgnoreCase(fn.name, name); });
}

[[nodiscard]] const il::core::Function *findFunctionCaseInsensitive(const il::core::Module &module,
                                                                    std::string_view name)
{
    for (const auto &fn : module.functions)
    {
        if (equalsIgnoreCase(fn.name, name))
            return &fn;
    }
    return nullptr;
}
} // namespace

TEST(BasicOOPLoweringTest, EmitsRuntimeHelpersAndClassMembers)
{
    SourceManager sourceManager;
    BasicCompilerInput input{kLoweringSnippet, "basic_oop.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sourceManager);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;

    EXPECT_TRUE(hasExtern(module, "rt_obj_new_i64"));
    EXPECT_TRUE(hasExtern(module, "rt_obj_release_check0"));
    EXPECT_TRUE(hasExtern(module, "rt_obj_free"));

    EXPECT_TRUE(hasFunction(module, "Klass.__ctor"));
    EXPECT_TRUE(hasFunction(module, "Klass.__dtor"));
    EXPECT_TRUE(hasFunction(module, "Klass.inc"));
}

TEST(BasicOOPLoweringTest, StoresMemberAssignmentIntoField)
{
    const std::string src = "10 CLASS C\n"
                             "20   v AS INTEGER\n"
                             "30   SUB Set7()\n"
                             "40     LET Me.v = 7\n"
                             "50   END SUB\n"
                             "60 END CLASS\n"
                             "70 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_set.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *set7 = findFunctionCaseInsensitive(module, "C.Set7");
    ASSERT_NE(set7, nullptr);

    bool sawStore = false;
    for (const auto &block : set7->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Store && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[1].i64 == 7)
            {
                sawStore = true;
                break;
            }
        }
        if (sawStore)
            break;
    }

    EXPECT_TRUE(sawStore);
}

TEST(BasicOOPLoweringTest, MethodParametersForwardedToCallee)
{
    const std::string src = "10 CLASS D\n"
                             "20   SUB Echo(v AS INTEGER)\n"
                             "30     PRINT v\n"
                             "40   END SUB\n"
                             "50 END CLASS\n"
                             "60 DIM d AS D\n"
                             "70 LET d = NEW D()\n"
                             "80 d.Echo(123)\n"
                             "90 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "method_params.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *method = findFunctionCaseInsensitive(module, "D.Echo");
    ASSERT_NE(method, nullptr);
    ASSERT_EQ(method->params.size(), 2u);
    EXPECT_TRUE(equalsIgnoreCase(method->params[0].name, "ME"));
    EXPECT_TRUE(equalsIgnoreCase(method->params[1].name, "v"));

    bool sawSelfStore = false;
    bool sawParamStore = false;
    if (!method->blocks.empty())
    {
        const auto &entry = method->blocks.front();
        for (const auto &instr : entry.instructions)
        {
            if (instr.op != il::core::Opcode::Store || instr.operands.size() < 2)
                continue;
            if (instr.operands[1].kind != il::core::Value::Kind::Temp)
                continue;
            if (instr.operands[1].id == method->params[0].id)
                sawSelfStore = true;
            if (instr.operands[1].id == method->params[1].id)
                sawParamStore = true;
        }
    }
    EXPECT_TRUE(sawSelfStore);
    EXPECT_TRUE(sawParamStore);

    const il::core::Function *mainFn = findFunctionCaseInsensitive(module, "main");
    ASSERT_NE(mainFn, nullptr);

    bool validatedCall = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::Call)
                continue;
            if (!equalsIgnoreCase(instr.callee, "D.Echo"))
                continue;
            ASSERT_EQ(instr.operands.size(), 2u);
            EXPECT_EQ(instr.operands[1].kind, il::core::Value::Kind::ConstInt);
            EXPECT_EQ(instr.operands[1].i64, 123);
            validatedCall = true;
        }
    }
    EXPECT_TRUE(validatedCall);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
