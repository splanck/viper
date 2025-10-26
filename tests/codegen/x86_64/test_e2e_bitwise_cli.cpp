// File: tests/codegen/x86_64/test_e2e_bitwise_cli.cpp
// Purpose: Validate that the ilc CLI produces identical stdout and exit codes
//          for VM and native execution paths when evaluating bitwise IL
//          programs.
// Key invariants: The temporary IL module exercises and/or/xor operations and
//                 must yield identical observable results regardless of the
//                 backend chosen.
// Ownership/Lifetime: Temporary files are created within the operating system
//                      temp directory and removed automatically after the test
//                      completes.
// Links: tools/ilc (CLI front-end bridging VM and native code generation).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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
constexpr const char kBitwiseProgram[] = R"(il 0.1.2
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

[[nodiscard]] std::filesystem::path makeTempPath(const std::string_view suffix)
{
    static std::uint64_t counter = 0;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const std::string unique = std::to_string(now.count()) + "_" +
                               std::to_string(++counter);
    std::string fileName = "viper_bitwise_cli_" + unique;
    fileName.append(suffix);
    return std::filesystem::temp_directory_path() / fileName;
}

struct TempFile
{
    explicit TempFile(const std::string_view suffix) : path(makeTempPath(suffix)) {}

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::filesystem::path path;
};

struct CommandRun
{
    int exitCode = -1;
    std::string stdoutText;
    std::string commandLine;
    bool systemCallFailed = false;
    bool stdoutReadFailed = false;
};

struct ComparisonResult
{
    bool success = false;
    std::string message;
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

[[nodiscard]] CommandRun runWithStdoutCapture(const std::string &command,
                                              const std::filesystem::path &stdoutPath)
{
    CommandRun run{};
    std::string commandWithRedirection = command + " > " + quoteForShell(stdoutPath);
    run.commandLine = commandWithRedirection;

    const int rawStatus = std::system(commandWithRedirection.c_str());
    run.exitCode = decodeExitCode(rawStatus);
    run.systemCallFailed = (rawStatus == -1);

    std::ifstream stdoutFile(stdoutPath);
    if (!stdoutFile.is_open())
    {
        run.stdoutReadFailed = true;
        return run;
    }

    std::ostringstream buffer;
    buffer << stdoutFile.rdbuf();
    run.stdoutText = buffer.str();
    return run;
}

[[nodiscard]] ComparisonResult verifyBitwiseCliParity()
{
    ComparisonResult result{};
    result.success = false;

    TempFile ilFile(".il");
    TempFile vmStdoutFile(".vm.stdout");
    TempFile nativeStdoutFile(".native.stdout");

    {
        std::ofstream ilStream(ilFile.path);
        if (!ilStream.is_open())
        {
            result.message = "Failed to create IL file at " + ilFile.path.string();
            return result;
        }
        ilStream << kBitwiseProgram;
        if (!ilStream.good())
        {
            result.message = "Failed to write IL program to " + ilFile.path.string();
            return result;
        }
    }

    const std::string vmCommand = "ilc -run " + quoteForShell(ilFile.path);
    const std::string nativeCommand =
        "ilc codegen x64 --run-native " + quoteForShell(ilFile.path);

    const CommandRun vmRun = runWithStdoutCapture(vmCommand, vmStdoutFile.path);
    const CommandRun nativeRun =
        runWithStdoutCapture(nativeCommand, nativeStdoutFile.path);

    bool success = true;
    std::ostringstream diff;

    if (vmRun.systemCallFailed)
    {
        success = false;
        diff << "VM invocation failed (std::system returned -1).\n";
    }
    if (nativeRun.systemCallFailed)
    {
        success = false;
        diff << "Native invocation failed (std::system returned -1).\n";
    }

    if (vmRun.stdoutReadFailed)
    {
        success = false;
        diff << "Failed to read VM stdout file: " << vmStdoutFile.path << "\n";
    }
    if (nativeRun.stdoutReadFailed)
    {
        success = false;
        diff << "Failed to read native stdout file: " << nativeStdoutFile.path
             << "\n";
    }

    if (success && vmRun.exitCode != nativeRun.exitCode)
    {
        success = false;
        diff << "Exit code mismatch. VM: " << vmRun.exitCode
             << ", Native: " << nativeRun.exitCode << "\n";
    }

    if (success && vmRun.stdoutText != nativeRun.stdoutText)
    {
        success = false;
        diff << "Stdout mismatch.\n";
        diff << "--- VM stdout ---\n" << vmRun.stdoutText;
        if (!vmRun.stdoutText.empty() && vmRun.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
        diff << "--- Native stdout ---\n" << nativeRun.stdoutText;
        if (!nativeRun.stdoutText.empty() && nativeRun.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
    }

    if (!success)
    {
        diff << "VM command: " << vmRun.commandLine << "\n";
        diff << "Native command: " << nativeRun.commandLine << "\n";
        result.message = diff.str();
        return result;
    }

    result.success = true;
    return result;
}
} // namespace

#if VIPER_HAS_GTEST

TEST(CodegenX64BitwiseCliTest, VmAndNativeCliOutputsMatch)
{
    const ComparisonResult result = verifyBitwiseCliParity();
    ASSERT_TRUE(result.success) << result.message;
}

#else

int main()
{
    const ComparisonResult result = verifyBitwiseCliParity();
    if (!result.success)
    {
        std::cerr << result.message;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
