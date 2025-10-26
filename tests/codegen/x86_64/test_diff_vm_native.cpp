// File: tests/codegen/x86_64/test_diff_vm_native.cpp
// Purpose: Ensure the ilc VM runner and native x86-64 backend produce identical
//          stdout streams and exit codes for an identical IL program.
// Key invariants: The inline IL program exercises integer/float printing and a
//                 conditional branch while remaining deterministic across
//                 execution modes.
// Ownership/Lifetime: Test owns temporary files/directories per invocation and
//                      cleans them up upon completion.
// Links: tools/ilc (command driver for VM and native execution paths).

#include <chrono>
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
struct CommandExecution
{
    int exitCode = -1;
    std::string stdoutText;
    std::string commandLine;
    bool systemCallFailed = false;
    bool stdoutReadFailed = false;
};

struct DiffCheckResult
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

[[nodiscard]] CommandExecution runWithStdoutCapture(const std::string &commandLine,
                                                    const std::filesystem::path &stdoutPath)
{
    CommandExecution execution{};
    execution.commandLine = commandLine;

    const int rawStatus = std::system(commandLine.c_str());
    execution.exitCode = decodeExitCode(rawStatus);
    execution.systemCallFailed = (rawStatus == -1);

    std::ifstream stdoutFile(stdoutPath);
    if (!stdoutFile.is_open())
    {
        execution.stdoutReadFailed = true;
        return execution;
    }

    std::ostringstream buffer;
    buffer << stdoutFile.rdbuf();
    execution.stdoutText = buffer.str();
    return execution;
}

[[nodiscard]] DiffCheckResult verifyVmNativeParity()
{
    const auto uniqueSuffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() /
        std::filesystem::path("viper_vm_native_diff_" + uniqueSuffix);

    std::error_code ec{};
    std::filesystem::create_directories(tempDir, ec);
    if (ec)
    {
        DiffCheckResult result{};
        result.success = false;
        result.message = "Failed to create temp directory: " + ec.message();
        return result;
    }

    struct DirectoryGuard
    {
        std::filesystem::path path;
        ~DirectoryGuard()
        {
            if (!path.empty())
            {
                std::error_code ignore{};
                std::filesystem::remove_all(path, ignore);
            }
        }
    } guard{tempDir};

    constexpr std::string_view kProgram = R"(il 0.1.2
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void

func @main() -> i32 {
entry:
  %condition = scmp_gt 5, 3
  cbr %condition, greater, smaller
greater:
  call @rt_print_i64(42)
  call @rt_print_f64(3.5)
  br exit
smaller:
  call @rt_print_i64(0)
  call @rt_print_f64(0.0)
  br exit
exit:
  ret 7
}
)";

    const std::filesystem::path ilPath = tempDir / "branch_print.il";
    {
        std::ofstream ilFile(ilPath, std::ios::binary);
        ilFile << kProgram;
        if (!ilFile)
        {
            DiffCheckResult result{};
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
        std::string("ilc codegen x64 ") + quoteForShell(ilPath) + " -run-native > " +
        quoteForShell(nativeStdoutPath);

    const CommandExecution vmExecution = runWithStdoutCapture(vmCommand, vmStdoutPath);
    const CommandExecution nativeExecution =
        runWithStdoutCapture(nativeCommand, nativeStdoutPath);

    bool success = true;
    std::ostringstream diff;

    if (vmExecution.systemCallFailed || nativeExecution.systemCallFailed)
    {
        success = false;
        diff << "System invocation failed.\n";
        diff << "VM status: " << (vmExecution.systemCallFailed ? "failed" : "ok") << '\n';
        diff << "Native status: " << (nativeExecution.systemCallFailed ? "failed" : "ok") << '\n';
    }

    if (vmExecution.stdoutReadFailed || nativeExecution.stdoutReadFailed)
    {
        success = false;
        diff << "Failed to read captured stdout.\n";
        diff << "VM stdout path: " << vmStdoutPath << '\n';
        diff << "Native stdout path: " << nativeStdoutPath << '\n';
    }

    if (vmExecution.exitCode != nativeExecution.exitCode)
    {
        success = false;
        diff << "Exit code mismatch.\n";
        diff << "  VM exit code: " << vmExecution.exitCode << '\n';
        diff << "  Native exit code: " << nativeExecution.exitCode << '\n';
    }

    if (vmExecution.stdoutText != nativeExecution.stdoutText)
    {
        success = false;
        diff << "Stdout mismatch.\n";
        diff << "--- VM stdout ---\n" << vmExecution.stdoutText;
        if (!vmExecution.stdoutText.empty() && vmExecution.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
        diff << "--- Native stdout ---\n" << nativeExecution.stdoutText;
        if (!nativeExecution.stdoutText.empty() && nativeExecution.stdoutText.back() != '\n')
        {
            diff << '\n';
        }
    }

    DiffCheckResult result{};
    result.success = success;
    if (!success)
    {
        diff << "VM command: " << vmExecution.commandLine << '\n';
        diff << "Native command: " << nativeExecution.commandLine << '\n';
        result.message = diff.str();
    }
    return result;
}
} // namespace

#if VIPER_HAS_GTEST

TEST(CodegenX64DiffVmNativeTest, VmAndNativeOutputsMatch)
{
    const DiffCheckResult result = verifyVmNativeParity();
    ASSERT_TRUE(result.success) << result.message;
}

#else

int main()
{
    const DiffCheckResult result = verifyVmNativeParity();
    if (!result.success)
    {
        std::cerr << result.message;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif
