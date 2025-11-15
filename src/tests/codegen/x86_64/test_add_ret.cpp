// File: tests/codegen/x86_64/test_add_ret.cpp
// Purpose: Verify the x86-64 backend emits expected assembly for a minimal
//          add/ret function using the adapter ILModule facade.
// Key invariants: The generated assembly must contain the global symbol
//                 directive and the canonical mov/add/ret sequence.
// Ownership/Lifetime: Test builds the IL module locally and verifies emitted
//                      assembly by value without external dependencies.
// Links: src/codegen/x86_64/Backend.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>
#include <string_view>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <cstdlib>
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

namespace viper::codegen::x64
{
namespace
{
[[nodiscard]] ILValue makeParam(int id) noexcept
{
    ILValue value{};
    value.kind = ILValue::Kind::I64;
    value.id = id;
    return value;
}

[[nodiscard]] ILModule makeAddModule()
{
    ILValue paramA = makeParam(0);
    ILValue paramB = makeParam(1);

    ILInstr addInstr{};
    addInstr.opcode = "add";
    addInstr.ops = {paramA, paramB};
    addInstr.resultId = 2;
    addInstr.resultKind = ILValue::Kind::I64;

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    ILValue sum{};
    sum.kind = ILValue::Kind::I64;
    sum.id = addInstr.resultId;
    retInstr.ops = {sum};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {paramA.id, paramB.id};
    entry.paramKinds = {paramA.kind, paramB.kind};
    entry.instrs = {addInstr, retInstr};

    ILFunction func{};
    func.name = "add";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool containsExpectedInstructions(const std::string &asmText)
{
    constexpr std::string_view kPatterns[] = {
        ".globl add", "movq %rdi, %rax", "addq %rsi, %rax", "ret"};

    for (const std::string_view pattern : kPatterns)
    {
        if (asmText.find(pattern) == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64AddRetTest, EmitsAddFunctionAssembly)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeAddModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(containsExpectedInstructions(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeAddModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !containsExpectedInstructions(result.asmText))
    {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
