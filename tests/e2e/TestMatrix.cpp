// File: tests/e2e/TestMatrix.cpp
// Purpose: Execute a suite of BASIC and IL programs under multiple execution engines
//          and assert that observable behaviour matches across engines.
// Key invariants: Engines producing non-matching exit codes or output streams cause the
//                 test to fail with a detailed diff for quick diagnosis.
// Ownership/Lifetime: The test owns subprocess invocations and temporary output strings.
// Links: src/tools/ilc/cmd_run_il.cpp, src/tools/ilc/cmd_front_basic.cpp

#include "common/RunProcess.hpp"
#include "vm/VMConfig.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef VIPER_ILC_PATH
#error "VIPER_ILC_PATH must be defined to locate the ilc executable"
#endif

#ifndef VIPER_SOURCE_DIR
#error "VIPER_SOURCE_DIR must be defined to locate test inputs"
#endif

#ifndef VIPER_TEST_MATRIX_HAS_NATIVE
#define VIPER_TEST_MATRIX_HAS_NATIVE 0
#endif

namespace
{

enum class ProgramKind
{
    Basic,
    Il,
};

struct Program
{
    std::string name;
    std::filesystem::path path;
    ProgramKind kind;
};

struct Engine
{
    std::string label;
    std::string cliValue;
};

struct ExecutionResult
{
    int exitCode = 0;
    std::string stdoutText;
    std::string stderrText;
};

/// @brief Replace Windows-style line endings with '\n' so comparisons are stable.
/// @param text Input captured from stdout or stderr.
/// @return Normalised copy with consistent newline characters.
std::string normalizeNewlines(const std::string &text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                continue;
            }
            normalized.push_back('\n');
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

/// @brief Execute the specified program under a given engine.
/// @param program Program descriptor pointing at a BASIC or IL file.
/// @param engine  Engine selection passed to ilc via --engine.
/// @param ilcPath Fully qualified path to the ilc executable.
/// @return Populated execution result on success; std::nullopt when ilc could not launch.
std::optional<ExecutionResult> runUnderEngine(const Program &program,
                                              const Engine &engine,
                                              const std::filesystem::path &ilcPath)
{
    std::vector<std::string> argv;
    argv.push_back(ilcPath.string());
    if (program.kind == ProgramKind::Basic)
    {
        argv.emplace_back("front");
        argv.emplace_back("basic");
        argv.emplace_back("-run");
    }
    else
    {
        argv.emplace_back("-run");
    }
    argv.push_back(program.path.string());
    argv.emplace_back("--engine=" + engine.cliValue);

    const RunResult result = run_process(argv);
    if (result.exit_code == -1)
    {
        std::cerr << "failed to launch ilc for program '" << program.name << "' using engine '"
                  << engine.label << "'\n";
        return std::nullopt;
    }

    ExecutionResult exec{};
    exec.exitCode = result.exit_code;
    exec.stdoutText = normalizeNewlines(result.out);
    exec.stderrText = normalizeNewlines(result.err);
    return exec;
}

/// @brief Report a difference between two engine runs and exit with failure.
/// @param programName Name of the program under test.
/// @param baselineLabel Engine used for the baseline result.
/// @param candidateLabel Engine whose output differed.
/// @param baseline Baseline execution outcome.
/// @param candidate Candidate execution outcome.
/// @return Always returns 1 to propagate failure from main().
int reportMismatch(const std::string &programName,
                   const std::string &baselineLabel,
                   const std::string &candidateLabel,
                   const ExecutionResult &baseline,
                   const ExecutionResult &candidate)
{
    std::cerr << "engine mismatch for program '" << programName << "' between '" << baselineLabel
              << "' and '" << candidateLabel << "'\n";
    if (baseline.exitCode != candidate.exitCode)
    {
        std::cerr << "  exit codes: " << baselineLabel << '=' << baseline.exitCode << ", "
                  << candidateLabel << '=' << candidate.exitCode << "\n";
    }
    if (baseline.stdoutText != candidate.stdoutText)
    {
        std::cerr << "  stdout (" << baselineLabel << "):\n" << baseline.stdoutText;
        if (!baseline.stdoutText.empty() && baseline.stdoutText.back() != '\n')
        {
            std::cerr << "\n";
        }
        std::cerr << "  stdout (" << candidateLabel << "):\n" << candidate.stdoutText;
        if (!candidate.stdoutText.empty() && candidate.stdoutText.back() != '\n')
        {
            std::cerr << "\n";
        }
    }
    if (baseline.stderrText != candidate.stderrText)
    {
        std::cerr << "  stderr (" << baselineLabel << "):\n" << baseline.stderrText;
        if (!baseline.stderrText.empty() && baseline.stderrText.back() != '\n')
        {
            std::cerr << "\n";
        }
        std::cerr << "  stderr (" << candidateLabel << "):\n" << candidate.stderrText;
        if (!candidate.stderrText.empty() && candidate.stderrText.back() != '\n')
        {
            std::cerr << "\n";
        }
    }
    return 1;
}

} // namespace

int main()
{
    const std::filesystem::path ilcPath(VIPER_ILC_PATH);
    const std::filesystem::path sourceRoot(VIPER_SOURCE_DIR);

    if (!std::filesystem::exists(ilcPath))
    {
        std::cerr << "ilc executable not found at " << ilcPath << "\n";
        return 1;
    }

    const std::vector<Program> programs = {
        {"basic_math_phase1.bas", sourceRoot / "tests/e2e/basic_math_phase1.bas", ProgramKind::Basic},
        {"factorial.bas", sourceRoot / "tests/e2e/factorial.bas", ProgramKind::Basic},
        {"simplifycfg_smoke.il", sourceRoot / "tests/e2e/simplifycfg_smoke.il", ProgramKind::Il},
    };

    std::vector<Engine> engines;
    engines.push_back({"vm-switch", "vm-switch"});
#if VIPER_THREADING_SUPPORTED
    engines.push_back({"vm-threaded", "vm-threaded"});
#else
    engines.push_back({"vm-table", "vm-table"});
#endif
#if VIPER_TEST_MATRIX_HAS_NATIVE
    engines.push_back({"native", "native"});
#endif

    if (engines.size() < 2)
    {
        std::cout << "engine matrix requires at least two engines; skipping comparisons\n";
        return 0;
    }

    for (const Program &program : programs)
    {
        std::vector<std::pair<std::string, ExecutionResult>> results;
        results.reserve(engines.size());

        for (const Engine &engine : engines)
        {
            auto exec = runUnderEngine(program, engine, ilcPath);
            if (!exec)
            {
                return 1;
            }
            results.emplace_back(engine.label, std::move(*exec));
        }

        if (results.size() <= 1)
        {
            continue;
        }

        const auto &baseline = results.front();
        for (size_t i = 1; i < results.size(); ++i)
        {
            const auto &candidate = results[i];
            if (baseline.second.exitCode != candidate.second.exitCode ||
                baseline.second.stdoutText != candidate.second.stdoutText ||
                baseline.second.stderrText != candidate.second.stderrText)
            {
                return reportMismatch(program.name,
                                      baseline.first,
                                      candidate.first,
                                      baseline.second,
                                      candidate.second);
            }
        }
    }

    return 0;
}
