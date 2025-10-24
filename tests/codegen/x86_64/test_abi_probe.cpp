// File: tests/codegen/x86_64/test_abi_probe.cpp
// Purpose: Ensure the x86-64 backend honours the SysV ABI when marshalling
//          mixed integer and floating-point arguments for external calls.
// Key invariants: The emitted assembly must move integer arguments into
//                 %rdi/%rsi/%rdx/%rcx/%r8/%r9, floating-point arguments into
//                 %xmm0-%xmm5, and establish a 16-byte-aligned stack frame.
// Ownership/Lifetime: The test builds an IL module locally and inspects the
//                      resulting assembly by value without running the code.
// Links: src/codegen/x86_64/CallLowering.cpp, src/codegen/x86_64/FrameLowering.cpp

#include "codegen/x86_64/Backend.hpp"

#include <array>
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
[[nodiscard]] ILValue makeParam(int id, ILValue::Kind kind) noexcept
{
    ILValue value{};
    value.kind = kind;
    value.id = id;
    return value;
}

[[nodiscard]] ILValue makeLabel(std::string name)
{
    ILValue value{};
    value.kind = ILValue::Kind::LABEL;
    value.label = std::move(name);
    return value;
}

[[nodiscard]] ILModule makeProbeModule()
{
    ILModule module{};

    ILFunction func{};
    func.name = "probe_caller";

    ILBlock entry{};
    entry.name = func.name;

    for (int i = 0; i < 6; ++i)
    {
        entry.paramIds.push_back(i);
        entry.paramKinds.push_back(ILValue::Kind::I64);
    }
    for (int i = 0; i < 6; ++i)
    {
        entry.paramIds.push_back(6 + i);
        entry.paramKinds.push_back(ILValue::Kind::F64);
    }

    ILInstr callInstr{};
    callInstr.opcode = "call";
    callInstr.ops.push_back(makeLabel("rt_probe_echo"));

    for (int i = 0; i < 6; ++i)
    {
        callInstr.ops.push_back(makeParam(i, ILValue::Kind::I64));
    }
    for (int i = 0; i < 6; ++i)
    {
        callInstr.ops.push_back(makeParam(6 + i, ILValue::Kind::F64));
    }

    ILInstr retInstr{};
    retInstr.opcode = "ret";

    entry.instrs.push_back(callInstr);
    entry.instrs.push_back(retInstr);

    func.blocks.push_back(entry);
    module.funcs.push_back(func);
    return module;
}

template <std::size_t N>
[[nodiscard]] bool containsAll(const std::string &asmText,
                               const std::array<std::string_view, N> &patterns)
{
    for (const std::string_view pattern : patterns)
    {
        if (asmText.find(pattern) == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool verifyProbeAssembly(const std::string &asmText)
{
    constexpr std::array<std::string_view, 6> kGprPatterns{
        ", %rdi", ", %rsi", ", %rdx", ", %rcx", ", %r8", ", %r9"};
    constexpr std::array<std::string_view, 6> kXmmPatterns{
        ", %xmm0", ", %xmm1", ", %xmm2", ", %xmm3", ", %xmm4", ", %xmm5"};
    constexpr std::array<std::string_view, 1> kAlignmentPattern{"addq $-8, %rsp"};

    return asmText.find("callq rt_probe_echo") != std::string::npos &&
           containsAll(asmText, kGprPatterns) && containsAll(asmText, kXmmPatterns) &&
           containsAll(asmText, kAlignmentPattern);
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64AbiProbeTest, EmitsRegisterAndAlignmentSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeProbeModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;
    EXPECT_TRUE(verifyProbeAssembly(result.asmText)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeProbeModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty() || !verifyProbeAssembly(result.asmText))
    {
        std::cerr << "Assembly verification failed:\n" << result.asmText;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
