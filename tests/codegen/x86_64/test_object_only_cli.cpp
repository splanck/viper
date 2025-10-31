// File: tests/codegen/x86_64/test_object_only_cli.cpp
// Purpose: Verify that the x86-64 codegen CLI can assemble IL modules without
//          linking when requested, ensuring object-only flows succeed.
// Key invariants: The CLI must successfully emit an object file even when the
//                 input module lacks an entry-point function such as @main.

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
#include <sys/wait.h>
#endif

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#define VIPER_HAS_GTEST 0
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

#if VIPER_HAS_GTEST

TEST(CodegenObjectOnlyCliTest, EmitsObjectWithoutMain)
{
    const ObjectOnlyResult result = runObjectOnlyCompileTest();
    ASSERT_TRUE(result.success) << result.message;
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#else

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

#endif
