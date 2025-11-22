//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/CodegenFixture.hpp
// Purpose: Provide reusable helpers for invoking the ilc CLI in end-to-end 
// Key invariants: Temporary filesystem artefacts are confined to a unique
// Ownership/Lifetime: The fixture owns its temporary directory for the
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace viper::tests
{
struct CodegenExecutionResult
{
    int exitCode = -1;
    std::string stdoutText;
    std::string commandLine;
    bool systemCallFailed = false;
    bool stdoutReadFailed = false;
};

struct CodegenComparisonOptions
{
    bool trimWhitespace = false;
    std::optional<double> numericTolerance;
};

struct CodegenComparisonResult
{
    bool success = false;
    std::string message;
    CodegenExecutionResult vm;
    CodegenExecutionResult native;
};

struct CodegenRunConfig
{
    std::string ilSource;
    std::string ilFileName = "program.il";
    std::vector<std::string> vmArgs;
    std::vector<std::string> nativeArgs;
};

class CodegenFixture
{
  public:
    CodegenFixture();
    CodegenFixture(const CodegenFixture &) = delete;
    CodegenFixture &operator=(const CodegenFixture &) = delete;
    CodegenFixture(CodegenFixture &&) = delete;
    CodegenFixture &operator=(CodegenFixture &&) = delete;
    ~CodegenFixture();

    [[nodiscard]] bool isReady() const noexcept
    {
        return setupError_.empty();
    }

    [[nodiscard]] const std::string &setupError() const noexcept
    {
        return setupError_;
    }

    [[nodiscard]] const std::filesystem::path &tempDirectory() const noexcept
    {
        return tempDir_;
    }

    [[nodiscard]] std::filesystem::path writeIlProgram(std::string_view fileName,
                                                       const std::string &source) const;

    [[nodiscard]] CodegenExecutionResult runVm(const std::filesystem::path &ilPath,
                                               const std::vector<std::string> &extraArgs = {});
    [[nodiscard]] CodegenExecutionResult runNative(const std::filesystem::path &ilPath,
                                                   const std::vector<std::string> &extraArgs = {});

    [[nodiscard]] CodegenComparisonResult compareVmAndNative(
        const CodegenRunConfig &config, const CodegenComparisonOptions &options);

  private:
    [[nodiscard]] std::filesystem::path reserveStdoutCapturePath(const std::string &stem);
    [[nodiscard]] CodegenExecutionResult runCommand(const std::string &commandLine,
                                                    const std::filesystem::path &stdoutPath) const;
    void removeAll() noexcept;

    std::filesystem::path tempDir_;
    std::string setupError_;
    std::size_t invocationId_ = 0;
};

/// @brief Quote @p path for use in shell command strings.
/// @details Wraps the string form of the path in double quotes and escapes
///          characters that carry special meaning for POSIX shells so tests can
///          forward paths verbatim to subprocesses. Windows keeps the historical
///          behaviour of only escaping embedded double quotes.
[[nodiscard]] std::string quoteForShell(const std::filesystem::path &path);

} // namespace viper::tests
