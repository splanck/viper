//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/BasicOOP_Lowering.cpp
// Purpose: Ensure BASIC OOP lowering emits runtime helpers and mangled members.
// Key invariants: Lowering produces required object runtime externs and class
// Ownership/Lifetime: Test owns compilation inputs and inspects resulting module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <unordered_map>

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

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawStore = false;
    bool storeUsesOffset = false;
    for (const auto &block : set7->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Store && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[1].i64 == 7)
            {
                sawStore = true;
                if (instr.operands[0].kind == il::core::Value::Kind::Temp)
                {
                    auto it = gepOffsets.find(instr.operands[0].id);
                    // Field offset is 8 (after vptr at offset 0)
                    if (it != gepOffsets.end() && it->second == 8)
                        storeUsesOffset = true;
                }
                break;
            }
        }
        if (sawStore)
            break;
    }

    EXPECT_TRUE(sawStore);
    EXPECT_TRUE(storeUsesOffset);
}

TEST(BasicOOPLoweringTest, StoresImplicitMemberAssignmentIntoField)
{
    const std::string src = "10 CLASS C\n"
                            "20   v AS INTEGER\n"
                            "30   SUB Set7()\n"
                            "40     Me.v = 7\n"
                            "50   END SUB\n"
                            "60 END CLASS\n"
                            "70 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_set_implicit.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *set7 = findFunctionCaseInsensitive(module, "C.Set7");
    ASSERT_NE(set7, nullptr);

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawStore = false;
    bool storeUsesOffset = false;
    for (const auto &block : set7->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Store && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[1].i64 == 7)
            {
                sawStore = true;
                if (instr.operands[0].kind == il::core::Value::Kind::Temp)
                {
                    auto it = gepOffsets.find(instr.operands[0].id);
                    // Field offset is 8 (after vptr at offset 0)
                    if (it != gepOffsets.end() && it->second == 8)
                        storeUsesOffset = true;
                }
                break;
            }
        }
        if (sawStore)
            break;
    }

    EXPECT_TRUE(sawStore);
    EXPECT_TRUE(storeUsesOffset);
}

TEST(BasicOOPLoweringTest, LoadsMemberAccessFromField)
{
    const std::string src = "10 CLASS C\n"
                            "20   v AS INTEGER\n"
                            "30   SUB Show()\n"
                            "40     LET Me.v = 42\n"
                            "50     PRINT Me.v\n"
                            "60   END SUB\n"
                            "70 END CLASS\n"
                            "80 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_load.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *showFn = findFunctionCaseInsensitive(module, "C.Show");
    ASSERT_NE(showFn, nullptr);

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawLoad = false;
    for (const auto &block : showFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Load && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                {
                    sawLoad = true;
                    break;
                }
            }
        }
        if (sawLoad)
            break;
    }

    EXPECT_TRUE(sawLoad);
}

TEST(BasicOOPLoweringTest, MemberFieldAccessibleAcrossMethods)
{
    const std::string src = "10 CLASS R\n"
                            "20   a AS INTEGER\n"
                            "30   SUB Set(v AS INTEGER)\n"
                            "40     LET Me.a = v\n"
                            "50   END SUB\n"
                            "60   FUNCTION Get%()\n"
                            "70     RETURN Me.a\n"
                            "80   END FUNCTION\n"
                            "90 END CLASS\n"
                            "100 DIM r AS R\n"
                            "110 LET r = NEW R()\n"
                            "120 r.Set(77)\n"
                            "130 PRINT r.Get%()\n"
                            "140 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_cross_methods.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;

    const il::core::Function *setFn = findFunctionCaseInsensitive(module, "R.Set");
    ASSERT_NE(setFn, nullptr);

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawFieldStore = false;
    for (const auto &block : setFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Store && instr.operands.size() >= 1 &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                    sawFieldStore = true;
            }
        }
    }

    EXPECT_TRUE(sawFieldStore);

    const il::core::Function *getFn = findFunctionCaseInsensitive(module, "R.Get%");
    ASSERT_NE(getFn, nullptr);

    gepOffsets.clear();
    bool sawFieldLoad = false;
    bool sawReturnFromLoad = false;
    unsigned loadedTemp = 0;
    for (const auto &block : getFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Load && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp && instr.result)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                {
                    sawFieldLoad = true;
                    loadedTemp = *instr.result;
                }
            }
            if (instr.op == il::core::Opcode::Ret && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                if (sawFieldLoad && instr.operands[0].id == loadedTemp)
                    sawReturnFromLoad = true;
            }
        }
    }

    EXPECT_TRUE(sawFieldLoad);
    EXPECT_TRUE(sawReturnFromLoad);
}

TEST(BasicOOPLoweringTest, MemberAccessOutsideMethodsStoresAndLoads)
{
    const std::string src = "10 CLASS D\n"
                            "20   v AS INTEGER\n"
                            "30 END CLASS\n"
                            "40 DIM d AS D\n"
                            "50 LET d = NEW D()\n"
                            "60 LET d.v = 9\n"
                            "70 PRINT d.v\n"
                            "80 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_main.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *mainFn = findFunctionCaseInsensitive(module, "main");
    ASSERT_NE(mainFn, nullptr);

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawStore = false;
    bool sawLoad = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Store && instr.operands.size() >= 2 &&
                instr.operands[0].kind == il::core::Value::Kind::Temp &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt &&
                instr.operands[1].i64 == 9)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                    sawStore = true;
            }
            if (instr.op == il::core::Opcode::Load && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                    sawLoad = true;
            }
        }
    }

    EXPECT_TRUE(sawStore);
    EXPECT_TRUE(sawLoad);
}

TEST(BasicOOPLoweringTest, MemberAccessStringFieldRetainsReferences)
{
    const std::string src = "10 CLASS P\n"
                            "20   name AS STRING\n"
                            "30 END CLASS\n"
                            "40 DIM p AS P\n"
                            "50 LET p = NEW P()\n"
                            "60 LET p.name = \"hi\"\n"
                            "70 LET p.name = \"bye\"\n"
                            "80 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "member_string.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *mainFn = findFunctionCaseInsensitive(module, "main");
    ASSERT_NE(mainFn, nullptr);

    bool sawRetain = false;
    bool sawRelease = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::Call)
                continue;
            if (equalsIgnoreCase(instr.callee, "rt_str_retain_maybe"))
                sawRetain = true;
            else if (equalsIgnoreCase(instr.callee, "rt_str_release_maybe"))
                sawRelease = true;
        }
    }

    EXPECT_TRUE(sawRetain);
    EXPECT_TRUE(sawRelease);
}

TEST(BasicOOPLoweringTest, BareFieldNameBindsToInstance)
{
    const std::string src = "10 CLASS C\n"
                            "20   v AS INTEGER\n"
                            "30   SUB Inc()\n"
                            "40     LET v = v + 1\n"
                            "50   END SUB\n"
                            "60 END CLASS\n"
                            "70 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "bare_field.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *incFn = findFunctionCaseInsensitive(module, "C.Inc");
    ASSERT_NE(incFn, nullptr);

    std::unordered_map<unsigned, long long> gepOffsets;
    bool sawLoad = false;
    bool sawStore = false;
    for (const auto &block : incFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::GEP && instr.result && instr.operands.size() >= 2 &&
                instr.operands[1].kind == il::core::Value::Kind::ConstInt)
            {
                gepOffsets.emplace(*instr.result, instr.operands[1].i64);
            }
            if (instr.op == il::core::Opcode::Load && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                    sawLoad = true;
            }
            if (instr.op == il::core::Opcode::Store && !instr.operands.empty() &&
                instr.operands[0].kind == il::core::Value::Kind::Temp)
            {
                auto it = gepOffsets.find(instr.operands[0].id);
                // Field offset is 8 (after vptr at offset 0)
                if (it != gepOffsets.end() && it->second == 8)
                    sawStore = true;
            }
        }
    }

    EXPECT_TRUE(sawLoad);
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

TEST(BasicOOPLoweringTest, MethodFunctionEmitsReturnValue)
{
    const std::string src = "10 CLASS M\n"
                            "20   FUNCTION Twice(n AS INTEGER) AS INTEGER\n"
                            "30     RETURN n + n\n"
                            "40   END FUNCTION\n"
                            "50 END CLASS\n"
                            "60 DIM m AS M\n"
                            "70 LET m = NEW M()\n"
                            "80 PRINT m.Twice(21)\n"
                            "90 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "method_return.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    const il::core::Function *method = findFunctionCaseInsensitive(module, "M.Twice");
    ASSERT_NE(method, nullptr);
    EXPECT_EQ(method->retType.kind, il::core::Type::Kind::I64);

    bool sawRetWithValue = false;
    for (const auto &block : method->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == il::core::Opcode::Ret && !instr.operands.empty())
            {
                sawRetWithValue = true;
                break;
            }
        }
        if (sawRetWithValue)
            break;
    }
    EXPECT_TRUE(sawRetWithValue);

    const il::core::Function *mainFn = findFunctionCaseInsensitive(module, "main");
    ASSERT_NE(mainFn, nullptr);
    bool sawCallResult = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::Call)
                continue;
            if (!equalsIgnoreCase(instr.callee, "M.Twice"))
                continue;
            if (instr.result.has_value())
            {
                sawCallResult = true;
                break;
            }
        }
        if (sawCallResult)
            break;
    }
    EXPECT_TRUE(sawCallResult);
}

TEST(BasicOOPLoweringTest, MethodFunctionSuffixReturnTypes)
{
    const std::string src = "10 CLASS P\n"
                            "20   FUNCTION Hello$()\n"
                            "30     RETURN \"hi\"\n"
                            "40   END FUNCTION\n"
                            "50   FUNCTION Half#()\n"
                            "60     RETURN 0.5\n"
                            "70   END FUNCTION\n"
                            "80   FUNCTION Count%()\n"
                            "90     RETURN 3\n"
                            "100  END FUNCTION\n"
                            "110 END CLASS\n"
                            "120 DIM p AS P\n"
                            "130 LET p = NEW P()\n"
                            "140 PRINT p.Hello$(), p.Half#(), p.Count%()\n"
                            "150 END\n";

    SourceManager sm;
    BasicCompilerInput input{src, "method_suffix.bas"};
    BasicCompilerOptions options{};

    auto result = compileBasic(input, options, sm);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;

    const il::core::Function *hello = findFunctionCaseInsensitive(module, "P.Hello$");
    ASSERT_NE(hello, nullptr);
    EXPECT_EQ(hello->retType.kind, il::core::Type::Kind::Str);

    const il::core::Function *half = findFunctionCaseInsensitive(module, "P.Half#");
    ASSERT_NE(half, nullptr);
    EXPECT_EQ(half->retType.kind, il::core::Type::Kind::F64);

    const il::core::Function *count = findFunctionCaseInsensitive(module, "P.Count%");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->retType.kind, il::core::Type::Kind::I64);

    const il::core::Function *mainFn = findFunctionCaseInsensitive(module, "main");
    ASSERT_NE(mainFn, nullptr);

    bool sawHelloCall = false;
    bool sawHalfCall = false;
    bool sawCountCall = false;
    for (const auto &block : mainFn->blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::Call)
                continue;
            if (equalsIgnoreCase(instr.callee, "P.Hello$"))
            {
                sawHelloCall = instr.result.has_value();
            }
            else if (equalsIgnoreCase(instr.callee, "P.Half#"))
            {
                sawHalfCall = instr.result.has_value();
            }
            else if (equalsIgnoreCase(instr.callee, "P.Count%"))
            {
                sawCountCall = instr.result.has_value();
            }
        }
    }

    EXPECT_TRUE(sawHelloCall);
    EXPECT_TRUE(sawHalfCall);
    EXPECT_TRUE(sawCountCall);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
