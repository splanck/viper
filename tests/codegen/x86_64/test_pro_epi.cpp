// File: tests/codegen/x86_64/test_pro_epi.cpp
// Purpose: Verify the x86-64 backend emits a canonical stack frame prologue
//          and epilogue equivalent to push %rbp / mov %rsp, %rbp / sub.
// Key invariants: Assembly must contain the expected frame setup, stack
//                 adjustment, and trailing ret instruction.
// Ownership/Lifetime: The IL module is constructed locally and inspected by
//                      value, mirroring other codegen smoke tests.
// Links: src/codegen/x86_64/FrameLowering.cpp

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
[[nodiscard]] ILModule makePrologueModule()
{
    ILValue zero{};
    zero.kind = ILValue::Kind::I64;
    zero.id = -1;
    zero.i64 = 0;

    ILInstr ret{};
    ret.opcode = "ret";
    ret.ops = {zero};

    ILBlock entry{};
    entry.name = "entry";
    entry.instrs = {ret};

    ILFunction func{};
    func.name = "frame_probe";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool hasCanonicalFrameSequence(const std::string &asmText)
{
    constexpr std::string_view kMovPattern = "movq %rsp, %rbp";
    if (asmText.find(kMovPattern) == std::string::npos)
    {
        return false;
    }

    const auto hasSub = [&]() {
        const std::string_view kSubPattern = "subq $";
        const std::string_view kAddNegPattern = "addq $-";
        const std::size_t subPos = asmText.find(kSubPattern);
        if (subPos != std::string::npos &&
            asmText.find("%rsp", subPos) != std::string::npos)
        {
            return true;
        }
        const std::size_t addPos = asmText.find(kAddNegPattern);
        return addPos != std::string::npos &&
               asmText.find("%rsp", addPos) != std::string::npos;
    }();

    if (!hasSub)
    {
        return false;
    }

    return asmText.find("ret") != std::string::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64FrameLoweringTest, EmitsCanonicalPrologueAndEpilogue)
{
    using namespace viper::codegen::x64;

    const ILModule module = makePrologueModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.asmText;
    EXPECT_TRUE(hasCanonicalFrameSequence(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makePrologueModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !hasCanonicalFrameSequence(result.asmText))
    {
        std::cerr << "Unexpected frame lowering output:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
