// File: tests/common/CodegenFixture.cpp
// Purpose: Implement helpers that orchestrate ilc CLI invocations for VM/native
//          parity tests.
// Key invariants: Command executions are serialized per fixture instance and
//                 stdout is captured via temporary files.
// Ownership/Lifetime: The fixture owns a temporary directory and removes it on
//                      destruction.

#include "tests/common/CodegenFixture.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace viper::tests
{
[[nodiscard]] std::string quoteForShell(const std::filesystem::path &path)
{
    const std::string raw = path.string();
    std::string quoted;
    quoted.reserve(raw.size() + 2);
    quoted.push_back('"');

    for (const char ch : raw)
    {
#ifdef _WIN32
        if (ch == '"')
        {
            quoted.push_back('\\');
        }
#else
        switch (ch)
        {
            case '\\':
            case '"':
            case '$':
            case '`':
                quoted.push_back('\\');
                break;
            default:
                break;
        }
#endif
        quoted.push_back(ch);
    }

    quoted.push_back('"');
    return quoted;
}

namespace
{
[[nodiscard]] int decodeExitCode(int rawStatus)
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

[[nodiscard]] std::string trimWhitespace(const std::string &text)
{
    const auto first =
        std::find_if_not(text.begin(),
                         text.end(),
                         [](unsigned char ch) { return static_cast<bool>(std::isspace(ch)); });
    if (first == text.end())
    {
        return std::string();
    }
    const auto last =
        std::find_if_not(text.rbegin(),
                         text.rend(),
                         [](unsigned char ch) { return static_cast<bool>(std::isspace(ch)); })
            .base();
    return std::string(first, last);
}

} // namespace

CodegenFixture::CodegenFixture()
{
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        setupError_ = "Failed to resolve temporary directory: " + ec.message();
        return;
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    tempDir_ = base / ("viper_codegen_fixture_" + std::to_string(static_cast<long long>(now)));

    std::filesystem::create_directories(tempDir_, ec);
    if (ec)
    {
        setupError_ = "Failed to create temporary directory: " + tempDir_.string() + " (" +
                      ec.message() + ")";
        tempDir_.clear();
    }
}

CodegenFixture::~CodegenFixture()
{
    removeAll();
}

std::filesystem::path CodegenFixture::writeIlProgram(std::string_view fileName,
                                                     const std::string &source) const
{
    if (!isReady())
    {
        return {};
    }

    const std::filesystem::path path = tempDir_ / std::filesystem::path(fileName);
    std::ofstream ilFile(path, std::ios::binary);
    ilFile << source;
    if (!ilFile)
    {
        return {};
    }
    return path;
}

CodegenExecutionResult CodegenFixture::runVm(const std::filesystem::path &ilPath,
                                             const std::vector<std::string> &extraArgs)
{
    const std::filesystem::path stdoutPath = reserveStdoutCapturePath("vm_stdout");
    std::ofstream(stdoutPath, std::ios::trunc).close();

    std::ostringstream command;
    command << "ilc -run " << quoteForShell(ilPath);
    for (const std::string &arg : extraArgs)
    {
        command << ' ' << arg;
    }
    command << " > " << quoteForShell(stdoutPath);

    return runCommand(command.str(), stdoutPath);
}

CodegenExecutionResult CodegenFixture::runNative(const std::filesystem::path &ilPath,
                                                 const std::vector<std::string> &extraArgs)
{
    const std::filesystem::path stdoutPath = reserveStdoutCapturePath("native_stdout");
    std::ofstream(stdoutPath, std::ios::trunc).close();

    std::ostringstream command;
    command << "ilc codegen x64 " << quoteForShell(ilPath) << " --run-native";
    for (const std::string &arg : extraArgs)
    {
        command << ' ' << arg;
    }
    command << " > " << quoteForShell(stdoutPath);

    return runCommand(command.str(), stdoutPath);
}

CodegenComparisonResult CodegenFixture::compareVmAndNative(const CodegenRunConfig &config,
                                                           const CodegenComparisonOptions &options)
{
    CodegenComparisonResult result{};

    if (!isReady())
    {
        result.success = false;
        result.message = setupError_;
        return result;
    }

    const std::filesystem::path ilPath = writeIlProgram(config.ilFileName, config.ilSource);
    if (ilPath.empty())
    {
        result.success = false;
        result.message = "Failed to write IL program to temporary directory.";
        return result;
    }

    result.vm = runVm(ilPath, config.vmArgs);
    result.native = runNative(ilPath, config.nativeArgs);

    bool success = true;
    std::ostringstream diff;

    if (result.vm.systemCallFailed || result.native.systemCallFailed)
    {
        success = false;
        diff << "System invocation failure.\n";
        diff << "VM system call status: " << (result.vm.systemCallFailed ? "failed" : "ok") << '\n';
        diff << "Native system call status: " << (result.native.systemCallFailed ? "failed" : "ok")
             << '\n';
    }

    if (result.vm.stdoutReadFailed || result.native.stdoutReadFailed)
    {
        success = false;
        diff << "Failed to read captured stdout.\n";
    }

    if (result.vm.exitCode != result.native.exitCode)
    {
        success = false;
        diff << "Exit code mismatch.\n";
        diff << "  VM exit code: " << result.vm.exitCode << '\n';
        diff << "  Native exit code: " << result.native.exitCode << '\n';
    }

    std::string vmStdout = result.vm.stdoutText;
    std::string nativeStdout = result.native.stdoutText;
    if (options.trimWhitespace)
    {
        vmStdout = trimWhitespace(vmStdout);
        nativeStdout = trimWhitespace(nativeStdout);
    }

    bool outputsMatch = vmStdout == nativeStdout;
    if (!outputsMatch && options.numericTolerance)
    {
        try
        {
            size_t vmPos = 0;
            size_t nativePos = 0;
            const double vmValue = std::stod(vmStdout, &vmPos);
            const double nativeValue = std::stod(nativeStdout, &nativePos);
            if (vmPos == vmStdout.size() && nativePos == nativeStdout.size())
            {
                outputsMatch = std::fabs(vmValue - nativeValue) <= *options.numericTolerance;
            }
        }
        catch (const std::exception &)
        {
            outputsMatch = false;
        }
    }

    if (!outputsMatch)
    {
        success = false;
        diff << "Stdout mismatch.\n";
        diff << "--- VM stdout ---\n" << vmStdout;
        if (!vmStdout.empty() && vmStdout.back() != '\n')
        {
            diff << '\n';
        }
        diff << "--- Native stdout ---\n" << nativeStdout;
        if (!nativeStdout.empty() && nativeStdout.back() != '\n')
        {
            diff << '\n';
        }
    }

    if (!success)
    {
        diff << "VM command: " << result.vm.commandLine << '\n';
        diff << "Native command: " << result.native.commandLine << '\n';
        result.message = diff.str();
    }

    result.success = success;
    return result;
}

std::filesystem::path CodegenFixture::reserveStdoutCapturePath(const std::string &stem)
{
    if (!isReady())
    {
        return {};
    }
    const std::filesystem::path path =
        tempDir_ / (stem + '_' + std::to_string(invocationId_++) + ".txt");
    return path;
}

CodegenExecutionResult CodegenFixture::runCommand(const std::string &commandLine,
                                                  const std::filesystem::path &stdoutPath) const
{
    CodegenExecutionResult execution{};
    execution.commandLine = commandLine;

    const int rawStatus = std::system(commandLine.c_str());
    execution.exitCode = decodeExitCode(rawStatus);
    execution.systemCallFailed = (rawStatus == -1);

    std::ifstream stdoutFile(stdoutPath, std::ios::binary);
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

void CodegenFixture::removeAll() noexcept
{
    if (tempDir_.empty())
    {
        return;
    }
    std::error_code ignore;
    std::filesystem::remove_all(tempDir_, ignore);
    tempDir_.clear();
}

} // namespace viper::tests
