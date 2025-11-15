// File: tests/codegen/x86_64/test_return_values.cpp
// Purpose: Ensure the x86-64 backend correctly copies return registers for
//          integer and floating-point values using the adapter ILModule.
// Key invariants: The emitted assembly must contain a mov into the canonical
//                 return register immediately before the function epilogue.
// Ownership/Lifetime: The IL module is constructed locally for the duration
//                      of the test and destroyed afterwards.
// Links: src/codegen/x86_64/Backend.cpp

#include "codegen/x86_64/Backend.hpp"

#include <regex>
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

[[nodiscard]] ILInstr makeReturnInstr(const ILValue &value)
{
    ILInstr instr{};
    instr.opcode = "ret";
    instr.ops = {value};
    return instr;
}

[[nodiscard]] ILModule makeReturnModule()
{
    ILValue i64Param = makeParam(0, ILValue::Kind::I64);
    ILValue f64Param = makeParam(0, ILValue::Kind::F64);

    ILBlock i64Entry{};
    i64Entry.name = "entry";
    i64Entry.paramIds = {i64Param.id};
    i64Entry.paramKinds = {i64Param.kind};
    i64Entry.instrs = {makeReturnInstr(i64Param)};

    ILFunction i64Func{};
    i64Func.name = "ret_i64";
    i64Func.blocks = {i64Entry};

    ILBlock f64Entry{};
    f64Entry.name = "entry";
    f64Entry.paramIds = {f64Param.id};
    f64Entry.paramKinds = {f64Param.kind};
    f64Entry.instrs = {makeReturnInstr(f64Param)};

    ILFunction f64Func{};
    f64Func.name = "ret_f64";
    f64Func.blocks = {f64Entry};

    ILModule module{};
    module.funcs = {i64Func, f64Func};
    return module;
}

[[nodiscard]] bool hasMovRetSequence(std::string_view asmText, const std::regex &movPattern)
{
    std::cmatch match;
    const char *const begin = asmText.data();
    const char *const end = begin + asmText.size();

    if (!std::regex_search(begin, end, match, movPattern))
    {
        return false;
    }

    const std::size_t movEndOffset =
        static_cast<std::size_t>(match.position()) + static_cast<std::size_t>(match.length());
    const std::size_t retPos = asmText.find("ret", movEndOffset);
    return retPos != std::string_view::npos;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64ReturnValuesTest, EmitsIntegerReturnMove)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeReturnModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.asmText;
    const std::regex movPattern{"movq %[^,]+, %rax"};
    EXPECT_TRUE(hasMovRetSequence(result.asmText, movPattern)) << result.asmText;
}

TEST(CodegenX64ReturnValuesTest, EmitsFloatReturnMove)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeReturnModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.asmText;
    const std::regex movPattern{"movsd [^,]+, %xmm0"};
    EXPECT_TRUE(hasMovRetSequence(result.asmText, movPattern)) << result.asmText;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeReturnModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << "Unexpected errors during codegen\n";
        return EXIT_FAILURE;
    }

    const std::regex intPattern{"movq %[^,]+, %rax"};
    const std::regex floatPattern{"movsd [^,]+, %xmm0"};

    if (!hasMovRetSequence(result.asmText, intPattern) ||
        !hasMovRetSequence(result.asmText, floatPattern))
    {
        std::cerr << "Assembly missing expected return moves:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
