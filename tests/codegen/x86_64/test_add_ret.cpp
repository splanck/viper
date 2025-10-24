// File: tests/codegen/x86_64/test_add_ret.cpp
// Purpose: Smoke-test the x86-64 backend assembly emission for a simple add.
// Key invariants: The emitted assembly preserves calling convention moves and
//                 produces the expected arithmetic and return instructions.
// Ownership/Lifetime: Test constructs temporary IL scaffolding used solely for
//                     verifying the backend facade output.
// Links: src/codegen/x86_64/Backend.hpp (entry point under test)

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

#include "codegen/x86_64/Backend.hpp"

#include <string>

using viper::codegen::x64::CodegenOptions;
using viper::codegen::x64::CodegenResult;
using viper::codegen::x64::ILBlock;
using viper::codegen::x64::ILFunction;
using viper::codegen::x64::ILInstr;
using viper::codegen::x64::ILModule;
using viper::codegen::x64::ILValue;

namespace
{

[[nodiscard]] ILValue makeI64Value(int id) noexcept
{
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILModule makeAddModule()
{
    ILModule module{};

    ILFunction function{};
    function.name = "add";

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {0, 1};
    entry.paramKinds = {ILValue::Kind::I64, ILValue::Kind::I64};

    ILInstr add{};
    add.opcode = "add";
    add.ops = {makeI64Value(0), makeI64Value(1)};
    add.resultId = 2;
    add.resultKind = ILValue::Kind::I64;
    entry.instrs.push_back(add);

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {makeI64Value(2)};
    entry.instrs.push_back(ret);

    function.blocks.push_back(entry);
    module.funcs.push_back(function);

    return module;
}

} // namespace

TEST(CodegenX64BackendTest, EmitsAddReturningFunction)
{
    const ILModule module = makeAddModule();
    const CodegenResult result = viper::codegen::x64::emitModuleToAssembly(module, CodegenOptions{});

    ASSERT_TRUE(result.errors.empty());
    ASSERT_FALSE(result.asmText.empty());

    const std::string &asmText = result.asmText;
    EXPECT_NE(std::string::npos, asmText.find(".globl add"));
    EXPECT_NE(std::string::npos, asmText.find("movq %rdi, %rax"));
    EXPECT_NE(std::string::npos, asmText.find("addq %rsi, %rax"));
    EXPECT_NE(std::string::npos, asmText.find("ret"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
