// File: tests/unit/test_run_process_quotes.cpp
// Purpose: Verify run_process correctly preserves shell-sensitive characters when quoting arguments.
// Key invariants: Quotes and backslashes inside arguments survive round-tripping through the helper.
// Ownership/Lifetime: RunProcess owns no persistent resources; the spawned process terminates immediately.
// Links: src/common/RunProcess.cpp

#include "common/RunProcess.hpp"

#include "GTestStub.hpp"

#include <filesystem>
#include <optional>
#include <system_error>
#include <string>
#include <vector>

namespace
{
std::string trim_trailing_newlines(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
        text.pop_back();
    }
    return text;
}
} // namespace

TEST(RunProcess, PreservesQuotesAndBackslashes)
{
    const std::string trickyArg = "value \"with quotes\" and backslash \\\\ tail";

    const RunResult result = run_process({"cmake", "-E", "echo", trickyArg});

    EXPECT_NE(-1, result.exit_code);
    EXPECT_EQ(trickyArg, trim_trailing_newlines(result.out));
}

TEST(RunProcess, ForwardsEnvironmentVariables)
{
    const std::string varName = "VIPER_RUN_PROCESS_TEST_VAR";
    const std::string varValue = "viper-test-value";
    const RunResult result = run_process({"cmake", "-E", "environment"}, std::nullopt,
                                         {{varName, varValue}});

    EXPECT_NE(-1, result.exit_code);
    const std::string expectedLine = varName + "=" + varValue;
    EXPECT_NE(std::string::npos, result.out.find(expectedLine));
}

TEST(RunProcess, AppliesWorkingDirectory)
{
    namespace fs = std::filesystem;

    const fs::path tempDir = fs::temp_directory_path() / fs::path("viper-run-process-cwd-test");
    std::error_code preCleanupEc;
    fs::remove_all(tempDir, preCleanupEc);
    fs::create_directories(tempDir);
    const struct DirectoryCleanup
    {
        fs::path path;
        ~DirectoryCleanup()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } cleanup{tempDir};

#if defined(_WIN32)
    const RunResult result = run_process({"cmd.exe", "/C", "cd"}, tempDir.string());
#else
    const RunResult result = run_process({"/bin/pwd"}, tempDir.string());
#endif

    EXPECT_EQ(0, result.exit_code);
    const std::string trimmed = trim_trailing_newlines(result.out);
    const fs::path reported(trimmed);
    std::error_code ec;
    const bool equivalent = fs::equivalent(tempDir, reported, ec);
    EXPECT_FALSE(ec);
    EXPECT_TRUE(equivalent);
}

#ifndef _WIN32
TEST(RunProcess, ReportsPosixExitStatus)
{
    const RunResult result = run_process({"sh", "-c", "exit 42"});

    EXPECT_EQ(42, result.exit_code);
}
#endif

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
