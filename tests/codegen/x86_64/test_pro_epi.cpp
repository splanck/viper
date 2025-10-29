// File: tests/codegen/x86_64/test_pro_epi.cpp
// Purpose: Verify the x86-64 backend emits a prologue/epilogue sequence that
//          mirrors the canonical push/mov/sub pattern when lowering a trivial
//          function.
// Key invariants: Assembly must contain a frame setup using mov %rsp, %rbp,
//                 a stack pointer decrement (or equivalent add of a negative
//                 immediate), and a terminating ret instruction.
// Ownership/Lifetime: Test builds a local IL module and validates the emitted
//                      assembly text without external dependencies.
// Links: src/codegen/x86_64/FrameLowering.cpp

#include "codegen/x86_64/Backend.hpp"

#include <string>

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
[[nodiscard]] ILModule makeTrivialModule()
{
    ILValue zero{};
    zero.kind = ILValue::Kind::I64;
    zero.i64 = 0;

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {zero};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {ret};

    ILFunction func{};
    func.name = "prologue_epilogue";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasCanonicalFrameSequence(const std::string &asmText)
{
    const bool hasFrameMove = asmText.find("movq %rsp, %rbp") != std::string::npos;
    const bool hasStackAdjust =
        asmText.find("subq $") != std::string::npos || asmText.find("addq $-") != std::string::npos;
    const bool hasRet = asmText.find("ret") != std::string::npos;
    return hasFrameMove && hasStackAdjust && hasRet;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64PrologueEpilogueTest, EmitsCanonicalFrameSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeTrivialModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    EXPECT_TRUE(result.errors.empty());
    EXPECT_TRUE(hasCanonicalFrameSequence(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeTrivialModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !hasCanonicalFrameSequence(result.asmText))
    {
        std::cerr << "Unexpected assembly output:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
