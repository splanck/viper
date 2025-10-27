// File: tests/unit/test_run_process_quotes.cpp
// Purpose: Verify run_process correctly preserves shell-sensitive characters when quoting arguments.
// Key invariants: Quotes and backslashes inside arguments survive round-tripping through the helper.
// Ownership/Lifetime: RunProcess owns no persistent resources; the spawned process terminates immediately.
// Links: src/common/RunProcess.cpp

#include "common/RunProcess.hpp"

#include "GTestStub.hpp"

#include <chrono>
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
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();
    const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path tempDir = tempRoot /
                                         std::filesystem::path("viper-run-process-" + std::to_string(uniqueSuffix));

    std::filesystem::create_directories(tempDir);

    const std::u8string tempDirUtf8 = tempDir.generic_u8string();
    const std::string tempDirString(tempDirUtf8.begin(), tempDirUtf8.end());

    const RunResult result = run_process({"cmake", "-E", "touch", "marker.txt"}, tempDirString);

    EXPECT_NE(-1, result.exit_code);
    EXPECT_TRUE(std::filesystem::exists(tempDir / "marker.txt"));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
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
