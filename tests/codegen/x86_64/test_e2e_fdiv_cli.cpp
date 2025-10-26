// File: tests/codegen/x86_64/test_e2e_fdiv_cli.cpp
// Purpose: Validate that executing an IL program containing fdiv via the ilc CLI
//          yields identical observable behaviour between the VM runner and the
//          native x86-64 backend.
// Key invariants: Temporary files/directories are created inside the system
//                 temp directory and cleaned up; exit codes and formatted
//                 floating-point outputs must match (within formatting
//                 tolerance) across execution modes.
// Ownership/Lifetime: The test owns all temporary filesystem artifacts and
//                      ensures their removal once execution completes.
// Links: tools/ilc (CLI bridging VM and native backends for IL execution).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
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

[[nodiscard]] CommandResult runWithStdoutCapture(const std::string &commandLine,
                                                const std::filesystem::path &stdoutPath)
{
    CommandResult result{};
    result.commandLine = commandLine;

    const int rawStatus = std::system(commandLine.c_str());
    result.exitCode = decodeExitCode(rawStatus);
    result.systemCallFailed = (rawStatus == -1);

    std::ifstream stdoutFile(stdoutPath, std::ios::binary);
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

[[nodiscard]] std::string trimWhitespace(const std::string &text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return static_cast<bool>(std::isspace(ch));
    });
    if (first == text.end())
    {
        return std::string();
    }
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return static_cast<bool>(std::isspace(ch));
    }).base();
    return std::string(first, last);
}

[[nodiscard]] std::optional<double> parseDouble(const std::string &text)
{
    try
    {
        size_t pos = 0;
        const double value = std::stod(text, &pos);
        if (pos == text.size())
        {
            return value;
        }
    }
    catch (const std::exception &)
    {
    }
    return std::nullopt;
}

[[nodiscard]] ComparisonResult compareVmAndNativeOutputs()
{
    ComparisonResult comparison{};

    std::error_code ec;
    const std::filesystem::path baseTemp = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        comparison.message = "Failed to resolve temp directory: " + ec.message();
        return comparison;
    }

    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path tempDir =
        baseTemp / ("viper_fdiv_cli_" + std::to_string(static_cast<long long>(now)));

    std::filesystem::create_directories(tempDir, ec);
    if (ec)
    {
        comparison.message = "Failed to create temp directory: " + tempDir.string() + " (" + ec.message() + ")";
        return comparison;
    }

    TempDirectoryGuard tempGuard(tempDir);

    const char *kProgram = R"(il 0.1.2
extern @rt_print_f64(f64) -> void
func @main() -> i64 {
entry:
  %x = fconst.f64 6.0
  %y = fconst.f64 2.0
  %z = fdiv.f64 %x, %y
  ; To force an observable side-effect, print %z (3.0)
  call @rt_print_f64(%z)
  ret 0
}
)";

    const std::filesystem::path ilPath = tempDir / "fdiv.il";
    {
        std::ofstream ilFile(ilPath, std::ios::binary);
        ilFile << kProgram;
        if (!ilFile)
        {
            comparison.message = "Failed to write IL program to " + ilPath.string();
            return comparison;
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
    const std::string nativeCommand = std::string("ilc codegen x64 --run-native ") + quoteForShell(ilPath) +
                                      " > " + quoteForShell(nativeStdoutPath);

    const CommandResult vmResult = runWithStdoutCapture(vmCommand, vmStdoutPath);
    const CommandResult nativeResult = runWithStdoutCapture(nativeCommand, nativeStdoutPath);

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

    const std::string trimmedVm = trimWhitespace(vmResult.stdoutText);
    const std::string trimmedNative = trimWhitespace(nativeResult.stdoutText);

    bool outputsMatch = trimmedVm == trimmedNative;
    if (!outputsMatch)
    {
        const std::optional<double> vmValue = parseDouble(trimmedVm);
        const std::optional<double> nativeValue = parseDouble(trimmedNative);
        if (vmValue && nativeValue)
        {
            outputsMatch = std::fabs(*vmValue - *nativeValue) <= 1e-12;
        }
    }

    if (!outputsMatch)
    {
        success = false;
        diff << "Stdout mismatch.\n";
        diff << "--- VM stdout (trimmed) ---\n" << trimmedVm << '\n';
        diff << "--- Native stdout (trimmed) ---\n" << trimmedNative << '\n';
    }

    comparison.success = success;
    if (!success)
    {
        diff << "VM command: " << vmResult.commandLine << '\n';
        diff << "Native command: " << nativeResult.commandLine << '\n';
        comparison.message = diff.str();
    }

    return comparison;
}
} // namespace

#if VIPER_HAS_GTEST

TEST(CodegenX64FdivCliTest, VmAndNativeOutputsMatch)
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
