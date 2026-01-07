//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_object_only_cli.cpp
// Purpose: Verify that the x86-64 codegen CLI can assemble IL modules without
// Key invariants: The CLI must successfully emit an object file even when the
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#if !defined(_WIN32)
#include "tests/common/WaitCompat.hpp"
#endif

namespace
{

#if defined(VIPER_ILC_PATH)
constexpr const char kIlcExecutable[] = VIPER_ILC_PATH;
#else
constexpr const char kIlcExecutable[] = "ilc";
#endif

[[nodiscard]] std::string quoteForShell(const std::filesystem::path &path)
{
#if defined(_WIN32)
    // On Windows, use native backslash separators
    std::filesystem::path native = path;
    native.make_preferred();
    const std::string pathStr = native.string();

    // Only quote if the path contains spaces or special characters
    bool needsQuoting = false;
    for (const char ch : pathStr)
    {
        if (ch == ' ' || ch == '\t' || ch == '&' || ch == '|' || ch == '<' || ch == '>' ||
            ch == '^' || ch == '(' || ch == ')' || ch == '"')
        {
            needsQuoting = true;
            break;
        }
    }

    if (!needsQuoting)
    {
        return pathStr;
    }

    std::string quoted = "\"";
    for (const char ch : pathStr)
    {
        if (ch == '"')
        {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
#else
    // On Unix, always quote and escape backslashes and double quotes
    std::string quoted = "\"";
    for (const char ch : path.string())
    {
        if (ch == '\\' || ch == '"')
        {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
#endif
}

[[nodiscard]] int decodeExitCode(int rawStatus)
{
#if defined(_WIN32)
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

class TempDirGuard
{
  public:
    TempDirGuard()
    {
        const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::ostringstream builder;
        builder << "viper_object_only-" << timestamp;
        std::filesystem::path candidate = std::filesystem::temp_directory_path() / builder.str();

        std::error_code ec;
        std::filesystem::create_directories(candidate, ec);
        if (!ec)
        {
            path_ = std::move(candidate);
        }
    }

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

struct ObjectOnlyResult
{
    bool success = false;
    std::string message;
};

ObjectOnlyResult runObjectOnlyCompileTest()
{
    ObjectOnlyResult result{};
    TempDirGuard tempDir;
    if (tempDir.path().empty())
    {
        result.message = "failed to create temporary directory";
        return result;
    }

    const std::filesystem::path ilPath = tempDir.path() / "module.il";
    const std::filesystem::path objPath = tempDir.path() / "module.o";

    const std::string ilSource = R"(il 0.1.2
func @helper() -> i64 {
entry:
  ret 0x2A
}
)";

    if (!writeTextFile(ilPath, ilSource))
    {
        result.message = "failed to write IL source";
        return result;
    }

    std::ostringstream command;
    command << quoteForShell(std::filesystem::path(kIlcExecutable)) << " codegen x64 "
            << quoteForShell(ilPath) << " -o " << quoteForShell(objPath);
    const std::string commandLine = command.str();
    const int rawStatus = std::system(commandLine.c_str());
    if (rawStatus == -1)
    {
        result.message = "system() failed while running: " + commandLine;
        return result;
    }

    const int exitCode = decodeExitCode(rawStatus);
    if (exitCode != 0)
    {
        std::ostringstream error;
        error << "ilc exited with status " << exitCode;
        result.message = error.str();
        return result;
    }

    if (!std::filesystem::exists(objPath))
    {
        result.message = "object file was not produced";
        return result;
    }

    std::error_code sizeEc;
    const std::uintmax_t size = std::filesystem::file_size(objPath, sizeEc);
    if (sizeEc)
    {
        result.message = "failed to query object file size";
        return result;
    }
    if (size == 0)
    {
        result.message = "object file is empty";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace

int main()
{
    const ObjectOnlyResult result = runObjectOnlyCompileTest();
    if (!result.success)
    {
        std::cerr << result.message;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
