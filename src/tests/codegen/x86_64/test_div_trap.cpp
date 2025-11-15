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
#include "common/CodegenFixture.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
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

[[nodiscard]] bool envFlagEnabled(const char *name) noexcept
{
    if (const char *value = std::getenv(name))
    {
        std::string_view view(value);
        if (view.empty())
        {
            return true;
        }
        if (view == "0" || view == "false" || view == "FALSE" || view == "False")
        {
            return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<std::string> nativeExecDisabledReason()
{
    if (envFlagEnabled("VIPER_TESTS_DISABLE_NATIVE_EXEC"))
    {
        return std::string("Native execution disabled via VIPER_TESTS_DISABLE_NATIVE_EXEC");
    }
    if (envFlagEnabled("VIPER_TESTS_DISABLE_SUBPROCESS"))
    {
        return std::string("Native execution disabled via VIPER_TESTS_DISABLE_SUBPROCESS");
    }
    return std::nullopt;
}

[[nodiscard]] int decodeExitCode(const int rawStatus)
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

[[nodiscard]] std::string makeRunNativeCommand(const std::filesystem::path &ilPath)
{
    return std::string("ilc codegen x64 ") + viper::tests::quoteForShell(ilPath) + " -run-native";
}

[[nodiscard]] bool writeTextFile(const std::filesystem::path &path, std::string_view contents)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }
    file << contents;
    return file.good();
}

class TempDirGuard
{
  public:
    explicit TempDirGuard(std::filesystem::path path) : path_(std::move(path)) {}

    TempDirGuard(const TempDirGuard &) = delete;
    TempDirGuard &operator=(const TempDirGuard &) = delete;
    TempDirGuard(TempDirGuard &&) = delete;
    TempDirGuard &operator=(TempDirGuard &&) = delete;

    ~TempDirGuard()
    {
        if (path_.empty())
        {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

struct NativeRunResult
{
    bool skipped{false};
    bool launchFailed{false};
    int exitCode{0};
    std::string command{};
    std::string message{};
};

[[nodiscard]] NativeRunResult runDivTrapNative()
{
    NativeRunResult result{};

    if (auto reason = nativeExecDisabledReason())
    {
        result.skipped = true;
        result.message = *reason;
        return result;
    }

    std::error_code tempEc;
    const std::filesystem::path baseTemp = std::filesystem::temp_directory_path(tempEc);
    if (tempEc)
    {
        result.launchFailed = true;
        result.message = std::string("Failed to resolve temporary directory: ") + tempEc.message();
        return result;
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempDir = baseTemp / (std::string("viper_div_trap_") + suffix);

    std::error_code createEc;
    std::filesystem::create_directories(tempDir, createEc);
    if (createEc)
    {
        result.launchFailed = true;
        result.message = std::string("Failed to create temporary directory '") + tempDir.string() +
                         "': " + createEc.message();
        return result;
    }

    TempDirGuard guard(tempDir);

    constexpr std::string_view kDivTrapProgram = R"(il 0.1.2

func @main() -> i64 {
entry:
  %q:i64 = sdiv.chk0 1, 0
  ret %q
}
)";

    const std::filesystem::path ilPath = tempDir / "div_trap.il";
    if (!writeTextFile(ilPath, kDivTrapProgram))
    {
        result.launchFailed = true;
        result.message = std::string("Failed to write IL program to '") + ilPath.string() + "'";
        return result;
    }

    const std::string command = makeRunNativeCommand(ilPath);
    result.command = command;

    const int rawStatus = std::system(command.c_str());
    const int exitCode = decodeExitCode(rawStatus);
    if (exitCode == -1)
    {
        result.launchFailed = true;
        result.message = std::string("Failed to execute '") + command + "'";
        return result;
    }

    result.exitCode = exitCode;
    result.message =
        std::string("Command '") + command + "' exited with code " + std::to_string(exitCode);
    return result;
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

TEST(CodegenX64DivTrapTest, RunNativeTrapExitsNonZero)
{
    using namespace viper::codegen::x64;

    const NativeRunResult result = runDivTrapNative();
    if (result.skipped)
    {
        GTEST_SKIP() << result.message;
    }

    ASSERT_FALSE(result.launchFailed) << result.message;
    EXPECT_NE(result.exitCode, 0) << "Expected non-zero exit code when running native trap. "
                                  << result.message;
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

    const NativeRunResult native = runDivTrapNative();
    if (!native.skipped)
    {
        if (native.launchFailed)
        {
            std::cerr << native.message << '\n';
            return EXIT_FAILURE;
        }
        if (native.exitCode == 0)
        {
            std::cerr
                << "Expected ilc run-native to exit with a non-zero status when dividing by zero.\n"
                << native.message << '\n';
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

#endif
