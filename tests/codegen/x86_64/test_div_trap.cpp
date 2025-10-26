// File: tests/codegen/x86_64/test_div_trap.cpp
// Purpose: Verify that signed 64-bit division emits a guarded trap sequence
//          before the IDIV instruction in the x86-64 backend.
// Key invariants: Generated assembly must test the divisor for zero, branch to
//                 the shared trap block, extend RAX into RDX via CQO, execute
//                 IDIV, and call the runtime trap when the divisor is zero.
// Ownership/Lifetime: The test builds an IL module locally, requests assembly
//                     emission by value, and analyses the resulting text in
//                     memory without additional allocations.
// Links: src/codegen/x86_64/LowerDiv.cpp

#include "codegen/x86_64/Backend.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

#ifndef _WIN32
#include <sys/wait.h>
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

[[nodiscard]] ILModule makeDivModule()
{
    ILValue dividend = makeParam(0);
    ILValue divisor = makeParam(1);

    ILInstr divInstr{};
    divInstr.opcode = "div";
    divInstr.ops = {dividend, divisor};
    divInstr.resultId = 2;
    divInstr.resultKind = ILValue::Kind::I64;

    ILInstr retInstr{};
    retInstr.opcode = "ret";
    ILValue quotient{};
    quotient.kind = ILValue::Kind::I64;
    quotient.id = divInstr.resultId;
    retInstr.ops = {quotient};

    ILBlock entry{};
    entry.name = "entry";
    entry.paramIds = {dividend.id, divisor.id};
    entry.paramKinds = {dividend.kind, divisor.kind};
    entry.instrs = {divInstr, retInstr};

    ILFunction func{};
    func.name = "div_guard";
    func.blocks = {entry};

    ILModule module{};
    module.funcs = {func};
    return module;
}

[[nodiscard]] bool isSelfTest(const std::string &line)
{
    const auto testPos = line.find("testq");
    if (testPos == std::string::npos)
    {
        return false;
    }

    const auto firstPercent = line.find('%', testPos);
    if (firstPercent == std::string::npos)
    {
        return false;
    }
    const auto commaPos = line.find(',', firstPercent);
    if (commaPos == std::string::npos)
    {
        return false;
    }

    const std::string firstReg = line.substr(firstPercent, commaPos - firstPercent);

    const auto secondPercent = line.find('%', commaPos);
    if (secondPercent == std::string::npos)
    {
        return false;
    }
    const auto secondEnd = line.find_first_of(", \t", secondPercent);
    const std::string secondReg = secondEnd == std::string::npos
                                      ? line.substr(secondPercent)
                                      : line.substr(secondPercent, secondEnd - secondPercent);

    return firstReg == secondReg;
}

struct DivTrapSequence
{
    bool hasSelfTest{false};
    bool hasTrapBranch{false};
    bool hasCqto{false};
    bool hasIdiv{false};
    bool hasTrapCall{false};
};

[[nodiscard]] std::string makeDivTrapProgramText()
{
    return R"IL(func @main() -> i32 {
entry:
  %q = div 42, 0
  ret 0
}
)IL";
}

[[nodiscard]] std::string quoteForShell(const std::filesystem::path &path)
{
    const std::string raw = path.string();
    std::string quoted;
    quoted.reserve(raw.size() + 2);
    quoted.push_back('"');
    for (const char ch : raw)
    {
        if (ch == '"')
        {
            quoted.push_back('\\');
            quoted.push_back('"');
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

[[nodiscard]] bool canRunSubprocesses() noexcept
{
    return std::system(nullptr) != 0;
}

[[nodiscard]] int decodeExitCode(const int rawStatus) noexcept
{
#ifdef _WIN32
    return rawStatus;
#else
    if (rawStatus == -1)
    {
        return -1;
    }
    if (WIFEXITED(rawStatus))
    {
        return WEXITSTATUS(rawStatus);
    }
    if (WIFSIGNALED(rawStatus))
    {
        return 128 + WTERMSIG(rawStatus);
    }
    return rawStatus;
#endif
}

struct RuntimeTrapResult
{
    bool commandProcessorUnavailable{false};
    bool tempDirFailed{false};
    bool programWriteFailed{false};
    bool systemCallFailed{false};
    int exitCode{-1};
    std::filesystem::path tempDir;
    std::filesystem::path programPath;
    std::string commandLine;
};

[[nodiscard]] RuntimeTrapResult runDivZeroProgramNative()
{
    RuntimeTrapResult result{};
    if (!canRunSubprocesses())
    {
        result.commandProcessorUnavailable = true;
        return result;
    }

    namespace fs = std::filesystem;
    const auto uniqueSuffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path tempDir = fs::temp_directory_path() / fs::path("viper-div-trap-" + uniqueSuffix);
    result.tempDir = tempDir;

    std::error_code dirError;
    fs::create_directories(tempDir, dirError);
    if (dirError)
    {
        result.tempDirFailed = true;
        return result;
    }

    const fs::path programPath = tempDir / "div_zero.il";
    result.programPath = programPath;
    {
        std::ofstream ilFile(programPath, std::ios::binary);
        ilFile << makeDivTrapProgramText();
        if (!ilFile)
        {
            result.programWriteFailed = true;
            return result;
        }
    }

    const std::string command =
        std::string("ilc codegen x64 ") + quoteForShell(programPath) + " --run-native";
    result.commandLine = command;

    const int rawStatus = std::system(command.c_str());
    result.systemCallFailed = (rawStatus == -1);
    result.exitCode = decodeExitCode(rawStatus);

    std::error_code cleanupError;
    fs::remove_all(tempDir, cleanupError);
    (void)cleanupError;

    return result;
}

[[nodiscard]] DivTrapSequence analyseDivTrapSequence(const std::string &asmText)
{
    DivTrapSequence sequence{};
    std::istringstream stream{asmText};
    std::string line{};
    while (std::getline(stream, line))
    {
        if (!sequence.hasSelfTest && isSelfTest(line))
        {
            sequence.hasSelfTest = true;
        }
        if (!sequence.hasTrapBranch && line.find("je ") != std::string::npos &&
            line.find(".Ltrap_div0") != std::string::npos)
        {
            sequence.hasTrapBranch = true;
        }
        if (!sequence.hasCqto && line.find("cqto") != std::string::npos)
        {
            sequence.hasCqto = true;
        }
        if (!sequence.hasIdiv && line.find("idivq") != std::string::npos)
        {
            sequence.hasIdiv = true;
        }
        if (!sequence.hasTrapCall && line.find("callq") != std::string::npos &&
            line.find("rt_trap_div0") != std::string::npos)
        {
            sequence.hasTrapCall = true;
        }
    }
    return sequence;
}

} // namespace
} // namespace viper::codegen::x64

#if VIPER_HAS_GTEST

TEST(CodegenX64DivTrapTest, EmitsGuardedDivisionSequence)
{
    using namespace viper::codegen::x64;

    const ILModule module = makeDivModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    ASSERT_TRUE(result.errors.empty()) << result.errors;

    const DivTrapSequence sequence = analyseDivTrapSequence(result.asmText);
    EXPECT_TRUE(sequence.hasSelfTest) << result.asmText;
    EXPECT_TRUE(sequence.hasTrapBranch) << result.asmText;
    EXPECT_TRUE(sequence.hasCqto) << result.asmText;
    EXPECT_TRUE(sequence.hasIdiv) << result.asmText;
    EXPECT_TRUE(sequence.hasTrapCall) << result.asmText;
}

TEST(CodegenX64DivTrapTest, RuntimeTrapTerminatesProcess)
{
    using namespace viper::codegen::x64;

    const RuntimeTrapResult result = runDivZeroProgramNative();
    if (result.commandProcessorUnavailable)
    {
        GTEST_SKIP() << "Command processor unavailable; skipping native run.";
    }

    ASSERT_FALSE(result.tempDirFailed) << "Failed to create temp dir: " << result.tempDir.string();
    ASSERT_FALSE(result.programWriteFailed)
        << "Failed to write IL program at " << result.programPath.string();
    ASSERT_FALSE(result.systemCallFailed) << "Failed to invoke command: " << result.commandLine;

    EXPECT_NE(result.exitCode, 0) << "Native execution unexpectedly succeeded. Command: "
                                  << result.commandLine;
}

#else

int main()
{
    using namespace viper::codegen::x64;

    const ILModule module = makeDivModule();
    const CodegenResult result = emitModuleToAssembly(module, {});

    if (!result.errors.empty())
    {
        std::cerr << result.errors;
        return EXIT_FAILURE;
    }

    const DivTrapSequence sequence = analyseDivTrapSequence(result.asmText);
    if (!sequence.hasSelfTest || !sequence.hasTrapBranch || !sequence.hasCqto ||
        !sequence.hasIdiv || !sequence.hasTrapCall)
    {
        std::cerr << "Missing guarded division pattern in assembly:\n" << result.asmText;
        return EXIT_FAILURE;
    }

    const RuntimeTrapResult runtime = runDivZeroProgramNative();
    if (!runtime.commandProcessorUnavailable)
    {
        if (runtime.tempDirFailed)
        {
            std::cerr << "Failed to create temp directory at " << runtime.tempDir << '\n';
            return EXIT_FAILURE;
        }
        if (runtime.programWriteFailed)
        {
            std::cerr << "Failed to write IL program to " << runtime.programPath << '\n';
            return EXIT_FAILURE;
        }
        if (runtime.systemCallFailed)
        {
            std::cerr << "Failed to invoke command: " << runtime.commandLine << '\n';
            return EXIT_FAILURE;
        }
        if (runtime.exitCode == 0)
        {
            std::cerr << "Native execution unexpectedly succeeded. Command: " << runtime.commandLine
                      << '\n';
            return EXIT_FAILURE;
        }
    }
    else
    {
        std::cout << "Skipping native runtime trap test (no command processor).\n";
    }

    return EXIT_SUCCESS;
}

#endif
