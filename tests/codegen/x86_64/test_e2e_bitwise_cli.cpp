// File: tests/codegen/x86_64/test_e2e_bitwise_cli.cpp
// Purpose: Validate that the ilc CLI produces identical results between the VM
//          runner and native x86-64 backend for bitwise instructions.
// Key invariants: Temporary files are confined to the system temp directory and
//                 cleaned up after execution; command exit codes/stdout must
//                 match between execution modes.
// Ownership/Lifetime: The test owns transient files/directories and ensures
//                      their removal at scope exit.
// Links: tools/ilc (command driver bridging VM and native codegen execution).

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

namespace
{
struct TempDirectoryGuard
{
    explicit TempDirectoryGuard(std::filesystem::path dir) : directory(std::move(dir)) {}

    ~TempDirectoryGuard()
    {
        if (!directory.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    }

    std::filesystem::path directory;
};

struct CommandResult
{
    int exitCode = -1;
    std::string stdoutText;
    std::string commandLine;
    bool systemCallFailed = false;
    bool stdoutReadFailed = false;
};

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

[[nodiscard]] CommandResult runWithStdoutCapture(const std::string &commandLine,
                                                const std::filesystem::path &stdoutPath)
{
    CommandResult result{};
    result.commandLine = commandLine;

    const int rawStatus = std::system(commandLine.c_str());
    result.exitCode = decodeExitCode(rawStatus);
    result.systemCallFailed = (rawStatus == -1);

    std::ifstream stdoutFile(stdoutPath);
    if (!stdoutFile.is_open())
    {
        result.stdoutReadFailed = true;
        return result;
    }

    std::ostringstream buffer;
    buffer << stdoutFile.rdbuf();
    result.stdoutText = buffer.str();
    return result;
}

struct ComparisonResult
{
    bool success = false;
    std::string message;
};

[[nodiscard]] ComparisonResult compareVmAndNativeOutputs()
{
    const auto uniqueSuffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempDirBase = std::filesystem::temp_directory_path();
    const std::filesystem::path tempDir = tempDirBase /
        std::filesystem::path("viper_e2e_bitwise_cli_" + uniqueSuffix);

    std::error_code fsError;
    std::filesystem::create_directories(tempDir, fsError);
    if (fsError)
    {
        ComparisonResult result{};
        result.success = false;
        result.message = "Failed to create temporary directory: " + tempDir.string() +
            ", error: " + fsError.message();
        return result;
    }

    TempDirectoryGuard tempGuard(tempDir);

    const char *kProgram = R"(il 0.1.2
func @main() -> i64 {
entry:
  %a = iconst.i64 0xFF00FF00
  %b = iconst.i64 0x00000100
  %c = and.i64 %a, %b
  %d = or.i64 %c, 0x2
  %e = xor.i64 %d, 0x5
  ret %e
}
)";

    const std::filesystem::path ilPath = tempDir / "bitwise.il";
    {
        std::ofstream ilFile(ilPath, std::ios::binary);
        ilFile << kProgram;
        if (!ilFile)
        {
            ComparisonResult result{};
            result.success = false;
            result.message = "Failed to write IL program to " + ilPath.string();
            return result;
        }
    }

    const std::filesystem::path vmStdoutPath = tempDir / "vm_stdout.txt";
    const std::filesystem::path nativeStdoutPath = tempDir / "native_stdout.txt";

    {
        std::ofstream(vmStdoutPath, std::ios::trunc).close();
        std::ofstream(nativeStdoutPath, std::ios::trunc).close();
    }

    const std::string vmCommand =
        std::string("ilc -run ") + quoteForShell(ilPath) + " > " + quoteForShell(vmStdoutPath);
    const std::string nativeCommand =
        std::string("ilc codegen x64 --run-native ") + quoteForShell(ilPath) + " > " +
        quoteForShell(nativeStdoutPath);

    const CommandResult vmResult = runWithStdoutCapture(vmCommand, vmStdoutPath);
    const CommandResult nativeResult =
        runWithStdoutCapture(nativeCommand, nativeStdoutPath);

    bool success = true;
    std::ostringstream diff;

    if (vmResult.systemCallFailed || nativeResult.systemCallFailed)
    {
        success = false;
        diff << "System invocation failure.\n";
        diff << "VM system call status: " << (vmResult.systemCallFailed ? "failed" : "ok") << '\n';
        diff << "Native system call status: " << (nativeResult.systemCallFailed ? "failed" : "ok") << '\n';
    }

    if (vmResult.stdoutReadFailed || nativeResult.stdoutReadFailed)
    {
        success = false;
        diff << "Failed to read captured stdout.\n";
        diff << "VM stdout path: " << vmStdoutPath << '\n';
        diff << "Native stdout path: " << nativeStdoutPath << '\n';
    }

    if (vmResult.exitCode != nativeResult.exitCode)
    {
        success = false;
        diff << "Exit code mismatch.\n";
        diff << "  VM exit code: " << vmResult.exitCode << '\n';
        diff << "  Native exit code: " << nativeResult.exitCode << '\n';
    }

    if (vmResult.stdoutText != nativeResult.stdoutText)
    {
        success = false;
        diff << "Stdout mismatch.\n";
        diff << "--- VM stdout ---\n" << vmResult.stdoutText;
        if (!vmResult.stdoutText.empty() && vmResult.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
        diff << "--- Native stdout ---\n" << nativeResult.stdoutText;
        if (!nativeResult.stdoutText.empty() && nativeResult.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
    }

    ComparisonResult result{};
    result.success = success;
    if (!success)
    {
        diff << "VM command: " << vmResult.commandLine << '\n';
        diff << "Native command: " << nativeResult.commandLine << '\n';
        result.message = diff.str();
    }

    return result;
}
} // namespace

#if VIPER_HAS_GTEST

TEST(CodegenX64BitwiseCliTest, VmAndNativeBitwiseOutputsMatch)
{
    const ComparisonResult comparison = compareVmAndNativeOutputs();
    ASSERT_TRUE(comparison.success) << comparison.message;
}

#else

int main()
{
    const ComparisonResult comparison = compareVmAndNativeOutputs();
    if (!comparison.success)
    {
        std::cerr << comparison.message;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
