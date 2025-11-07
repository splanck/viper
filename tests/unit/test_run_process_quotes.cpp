// File: tests/unit/test_run_process_quotes.cpp
// Purpose: Verify run_process correctly preserves shell-sensitive characters when quoting
// arguments. Key invariants: Quotes and backslashes inside arguments survive round-tripping through
// the helper. Ownership/Lifetime: RunProcess owns no persistent resources; the spawned process
// terminates immediately. Links: src/common/RunProcess.cpp

#include "common/RunProcess.hpp"

#include "GTestStub.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
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

namespace viper::test_support
{
struct ScopedEnvironmentAssignmentMoveResult
{
    bool value_visible_after_move_ctor;
    bool value_visible_after_move_assign;
    bool restored;
    std::optional<std::string> move_assigned_value;
};

ScopedEnvironmentAssignmentMoveResult scoped_environment_assignment_move_preserves(
    const std::string &name, const std::string &source_value, const std::string &receiver_value);
} // namespace viper::test_support

TEST(RunProcess, PreservesQuotesAndBackslashes)
{
    const std::string trickyArg = "value \"with quotes\" and backslash \\\\ tail";

    const RunResult result = run_process({"cmake", "-E", "echo", trickyArg});

    EXPECT_NE(-1, result.exit_code);
    EXPECT_EQ(trickyArg, trim_trailing_newlines(result.out));
}

#ifndef _WIN32
TEST(RunProcess, EscapesPosixShellExpansions)
{
    const std::string trickyArg = "literal $PATH and `uname` markers";

    const RunResult result = run_process({"cmake", "-E", "echo", trickyArg});

    EXPECT_NE(-1, result.exit_code);
    EXPECT_EQ(trickyArg, trim_trailing_newlines(result.out));
}
#endif

TEST(RunProcess, ForwardsEnvironmentVariables)
{
    const std::string varName = "VIPER_RUN_PROCESS_TEST_VAR";
    const std::string varValue = "viper-test-value";
    const RunResult result =
        run_process({"cmake", "-E", "environment"}, std::nullopt, {{varName, varValue}});

    EXPECT_NE(-1, result.exit_code);
    const std::string expectedLine = varName + "=" + varValue;
    EXPECT_NE(std::string::npos, result.out.find(expectedLine));
}

TEST(RunProcess, ScopedEnvironmentAssignmentSurvivesMove)
{
    const std::string varName = "VIPER_SCOPED_ENV_MOVE_TEST";
    const std::string varValue = "scoped-env-move-value";

    const auto result =
        viper::test_support::scoped_environment_assignment_move_preserves(varName, varValue, varValue);

    EXPECT_TRUE(result.value_visible_after_move_ctor);
    EXPECT_TRUE(result.value_visible_after_move_assign);
    EXPECT_TRUE(result.restored);
}

TEST(RunProcess, ScopedEnvironmentAssignmentMoveAssignmentPrefersSourceValue)
{
    const std::string varName = "VIPER_SCOPED_ENV_MOVE_ASSIGN_TEST";
    const std::string sourceValue = "scoped-env-source-value";
    const std::string receiverValue = "scoped-env-receiver-value";

    const auto result = viper::test_support::scoped_environment_assignment_move_preserves(
        varName, sourceValue, receiverValue);

    EXPECT_TRUE(result.value_visible_after_move_ctor);
    ASSERT_TRUE(result.move_assigned_value.has_value());
    EXPECT_EQ(sourceValue, *result.move_assigned_value);
    EXPECT_TRUE(result.value_visible_after_move_assign);
    EXPECT_TRUE(result.restored);
}

TEST(RunProcess, AppliesWorkingDirectory)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();
    const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path tempDir =
        tempRoot / std::filesystem::path("viper-run-process-" + std::to_string(uniqueSuffix));

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
#else
TEST(RunProcess, CapturesWindowsStderr)
{
    const RunResult result = run_process({"cmd", "/C", "echo viper-stderr-sample 1>&2"});

    EXPECT_NE(-1, result.exit_code);
    const std::string trimmed = trim_trailing_newlines(result.err);
    EXPECT_NE(std::string::npos, trimmed.find("viper-stderr-sample"));
}
#endif

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
