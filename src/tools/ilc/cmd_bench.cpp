//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc bench` subcommand that benchmarks IL programs using
// different VM dispatch strategies. Outputs parse-friendly metrics including
// instruction count, wall-clock time, and instructions per second.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the `ilc bench` subcommand.
/// @details Provides CLI parsing for benchmark configuration, runs IL programs
///          with each dispatch strategy, and reports performance metrics.

#include "cli.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tools/common/module_loader.hpp"
#include "viper/vm/VM.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
// Windows doesn't have setenv, use _putenv_s instead
inline int setenv(const char *name, const char *value, int /*overwrite*/)
{
    return _putenv_s(name, value);
}
#endif

using namespace il;

namespace
{

/// @brief Benchmark configuration parsed from command-line.
struct BenchConfig
{
    std::vector<std::string> ilFiles;
    uint32_t iterations = 3;
    uint64_t maxSteps = 0;
    bool runTable = true;
    bool runSwitch = true;
    bool runThreaded = true;
    bool jsonOutput = false;
    bool verbose = false;
};

/// @brief Result of a single benchmark run.
struct BenchResult
{
    std::string file;
    std::string strategy;
    uint64_t instructions = 0;
    double timeMs = 0.0;
    double insnsPerSec = 0.0;
    int64_t returnValue = 0;
    bool success = true;
};

/// @brief Print usage information for the bench subcommand.
void benchUsage()
{
    std::cerr << "Usage: ilc bench <file.il> [file2.il ...] [options]\n"
              << "Options:\n"
              << "  -n <N>            Number of iterations (default: 3)\n"
              << "  --max-steps <N>   Maximum interpreter steps (0 = unlimited)\n"
              << "  --table           Run only FnTable dispatch\n"
              << "  --switch          Run only Switch dispatch\n"
              << "  --threaded        Run only Threaded dispatch\n"
              << "  --json            Output results as JSON\n"
              << "  -v, --verbose     Verbose output\n"
              << "\n"
              << "Output format (one line per file/strategy):\n"
              << "  BENCH <file> <strategy> instr=<N> time_ms=<T> insns_per_sec=<R>\n";
}

/// @brief Parse benchmark command-line arguments.
/// @param argc Number of arguments.
/// @param argv Argument vector.
/// @param config Output configuration.
/// @return True if parsing succeeded, false on error.
bool parseBenchArgs(int argc, char **argv, BenchConfig &config)
{
    bool strategySpecified = false;

    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-n")
        {
            if (i + 1 >= argc)
            {
                benchUsage();
                return false;
            }
            config.iterations = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--max-steps")
        {
            if (i + 1 >= argc)
            {
                benchUsage();
                return false;
            }
            config.maxSteps = std::stoull(argv[++i]);
        }
        else if (arg == "--table")
        {
            if (!strategySpecified)
            {
                config.runTable = false;
                config.runSwitch = false;
                config.runThreaded = false;
                strategySpecified = true;
            }
            config.runTable = true;
        }
        else if (arg == "--switch")
        {
            if (!strategySpecified)
            {
                config.runTable = false;
                config.runSwitch = false;
                config.runThreaded = false;
                strategySpecified = true;
            }
            config.runSwitch = true;
        }
        else if (arg == "--threaded")
        {
            if (!strategySpecified)
            {
                config.runTable = false;
                config.runSwitch = false;
                config.runThreaded = false;
                strategySpecified = true;
            }
            config.runThreaded = true;
        }
        else if (arg == "--json")
        {
            config.jsonOutput = true;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            config.verbose = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            benchUsage();
            return false;
        }
        else if (arg[0] == '-')
        {
            std::cerr << "Unknown option: " << arg << "\n";
            benchUsage();
            return false;
        }
        else
        {
            config.ilFiles.push_back(arg);
        }
    }

    if (config.ilFiles.empty())
    {
        std::cerr << "No input files specified\n";
        benchUsage();
        return false;
    }

    return true;
}

/// @brief Run a single benchmark iteration.
/// @param mod Module to execute.
/// @param strategy Dispatch strategy name ("table", "switch", "threaded").
/// @param maxSteps Maximum steps (0 = unlimited).
/// @return Benchmark result.
BenchResult runBenchmarkIteration(const core::Module &mod,
                                  const std::string &strategy,
                                  uint64_t maxSteps)
{
    BenchResult result;
    result.strategy = strategy;

    // Set dispatch strategy via environment variable
    setenv("VIPER_DISPATCH", strategy.c_str(), 1);

    // Create Runner with minimal configuration
    vm::RunConfig runCfg;
    runCfg.maxSteps = maxSteps;

    auto start = std::chrono::steady_clock::now();

    try
    {
        vm::Runner runner(mod, std::move(runCfg));
        result.returnValue = runner.run();
        result.instructions = runner.instructionCount();
    }
    catch (const std::exception &e)
    {
        result.success = false;
        return result;
    }

    auto end = std::chrono::steady_clock::now();
    result.timeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    if (result.timeMs > 0)
    {
        result.insnsPerSec = (result.instructions / result.timeMs) * 1000.0;
    }

    return result;
}

/// @brief Compute median of a vector of doubles.
double computeMedian(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    if (n % 2 == 0)
        return (values[n / 2 - 1] + values[n / 2]) / 2.0;
    return values[n / 2];
}

/// @brief Run benchmarks for a single file.
/// @param file Path to IL file.
/// @param config Benchmark configuration.
/// @param results Output vector of results.
/// @return True if benchmarking succeeded.
bool benchmarkFile(const std::string &file,
                   const BenchConfig &config,
                   std::vector<BenchResult> &results)
{
    il::support::SourceManager sm;
    core::Module mod;

    auto load = il::tools::common::loadModuleFromFile(file, mod, std::cerr);
    if (!load.succeeded())
    {
        std::cerr << "Failed to load: " << file << "\n";
        return false;
    }

    if (!il::tools::common::verifyModule(mod, std::cerr, &sm))
    {
        std::cerr << "Verification failed: " << file << "\n";
        return false;
    }

    std::vector<std::string> strategies;
    if (config.runTable)
        strategies.push_back("table");
    if (config.runSwitch)
        strategies.push_back("switch");
    if (config.runThreaded)
        strategies.push_back("threaded");

    for (const auto &strategy : strategies)
    {
        std::vector<double> times;
        std::vector<double> insnsPerSec;
        uint64_t instructions = 0;
        int64_t returnValue = 0;
        bool allSuccess = true;

        if (config.verbose)
        {
            std::cerr << "Running " << file << " with " << strategy << " (" << config.iterations
                      << " iterations)...\n";
        }

        for (uint32_t iter = 0; iter < config.iterations; ++iter)
        {
            auto iterResult = runBenchmarkIteration(mod, strategy, config.maxSteps);
            if (!iterResult.success)
            {
                allSuccess = false;
                break;
            }
            times.push_back(iterResult.timeMs);
            insnsPerSec.push_back(iterResult.insnsPerSec);
            instructions = iterResult.instructions;
            returnValue = iterResult.returnValue;
        }

        BenchResult result;
        result.file = file;
        result.strategy = strategy;
        result.success = allSuccess;
        result.instructions = instructions;
        result.returnValue = returnValue;

        if (allSuccess && !times.empty())
        {
            result.timeMs = computeMedian(times);
            result.insnsPerSec = computeMedian(insnsPerSec);
        }

        results.push_back(result);
    }

    return true;
}

/// @brief Print results in text format.
void printTextResults(const std::vector<BenchResult> &results)
{
    for (const auto &r : results)
    {
        if (!r.success)
        {
            std::cout << "BENCH " << r.file << " " << r.strategy << " FAILED\n";
            continue;
        }
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "BENCH " << r.file << " " << r.strategy << " instr=" << r.instructions
                  << " time_ms=" << r.timeMs << " insns_per_sec=" << std::setprecision(0)
                  << r.insnsPerSec << "\n";
    }
}

/// @brief Print results in JSON format.
void printJsonResults(const std::vector<BenchResult> &results)
{
    std::cout << "[\n";
    for (size_t i = 0; i < results.size(); ++i)
    {
        const auto &r = results[i];
        std::cout << "  {\n";
        std::cout << "    \"file\": \"" << r.file << "\",\n";
        std::cout << "    \"strategy\": \"" << r.strategy << "\",\n";
        std::cout << "    \"success\": " << (r.success ? "true" : "false") << ",\n";
        std::cout << "    \"instructions\": " << r.instructions << ",\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "    \"time_ms\": " << r.timeMs << ",\n";
        std::cout << std::setprecision(0);
        std::cout << "    \"insns_per_sec\": " << r.insnsPerSec << ",\n";
        std::cout << "    \"return_value\": " << r.returnValue << "\n";
        std::cout << "  }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    std::cout << "]\n";
}

} // namespace

/// @brief Handle the `ilc bench` subcommand.
/// @param argc Number of arguments after "bench".
/// @param argv Argument vector.
/// @return Exit status; 0 on success.
int cmdBench(int argc, char **argv)
{
    BenchConfig config;
    if (!parseBenchArgs(argc, argv, config))
    {
        return 1;
    }

    std::vector<BenchResult> allResults;

    for (const auto &file : config.ilFiles)
    {
        if (!benchmarkFile(file, config, allResults))
        {
            // Continue with other files
        }
    }

    if (allResults.empty())
    {
        std::cerr << "No benchmark results\n";
        return 1;
    }

    if (config.jsonOutput)
    {
        printJsonResults(allResults);
    }
    else
    {
        printTextResults(allResults);
    }

    return 0;
}
