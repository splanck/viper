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

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "cli.hpp"
#include "il/core/Module.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "tools/common/module_loader.hpp"
#include "viper/vm/VM.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>

/// @brief POSIX setenv shim for Windows, implemented via _putenv_s.
/// @note The overwrite flag is ignored; _putenv_s always overwrites.
inline int setenv(const char *name, const char *value, int /*overwrite*/) {
    return _putenv_s(name, value);
}
#endif

using namespace il;

namespace {

/// @brief Benchmark configuration parsed from command-line.
struct BenchConfig {
    std::vector<std::string> ilFiles;
    uint32_t iterations = 3;
    uint64_t maxSteps = 0;
    bool runTable = true;
    bool runSwitch = true;
    bool runThreaded = true;
    bool runBytecodeSwitch = false;
    bool runBytecodeThreaded = false;
    bool jsonOutput = false;
    bool verbose = false;
};

/// @brief Outcome of parsing the bench subcommand arguments.
enum class BenchParseResult {
    Ok,    ///< Arguments parsed successfully into the config.
    Help,  ///< Help was requested; caller should exit successfully.
    Error, ///< Arguments were invalid; usage already printed.
};

/// @brief Result of a single benchmark run.
struct BenchResult {
    std::string file;
    std::string strategy;
    uint64_t instructions = 0;
    double timeMs = 0.0;
    double insnsPerSec = 0.0;
    int64_t returnValue = 0;
    bool success = true;
};

/// @brief Print usage information for the bench subcommand.
void benchUsage() {
    std::cerr << "Usage: viper bench <file.il> [file2.il ...] [options]\n"
              << "Options:\n"
              << "  -n <N>            Number of iterations (default: 3)\n"
              << "  --max-steps <N>   Maximum interpreter steps (0 = unlimited)\n"
              << "  --table           Run only FnTable dispatch (standard VM)\n"
              << "  --switch          Run only Switch dispatch (standard VM)\n"
              << "  --threaded        Run only Threaded dispatch (standard VM)\n"
              << "  --bc-switch       Run Bytecode VM with switch dispatch\n"
              << "  --bc-threaded     Run Bytecode VM with threaded dispatch\n"
              << "  --bytecode        Run both Bytecode VM dispatch strategies\n"
              << "  --all             Run all dispatch strategies (default if none specified)\n"
              << "  --json            Output results as JSON\n"
              << "  -v, --verbose     Verbose output\n"
              << "\n"
              << "Output format (one line per file/strategy):\n"
              << "  BENCH <file> <strategy> instr=<N> time_ms=<T> insns_per_sec=<R>\n";
}

/// @brief Start an explicit benchmark strategy selection when the first specific flag is seen.
/// @details The default selection runs the standard VM strategies. Once a specific strategy flag
/// is parsed, all strategies are cleared and subsequent specific flags add to that set. `--all`
/// resets this mode so a later specific flag can intentionally narrow the selection again.
void beginExplicitStrategySelection(BenchConfig &config, bool &strategySpecified) {
    if (strategySpecified)
        return;
    config.runTable = false;
    config.runSwitch = false;
    config.runThreaded = false;
    config.runBytecodeSwitch = false;
    config.runBytecodeThreaded = false;
    strategySpecified = true;
}

/// @brief Parse benchmark command-line arguments.
/// @param argc Number of arguments.
/// @param argv Argument vector.
/// @param config Output configuration.
/// @return True if parsing succeeded, false on error.
bool parseBenchArgs(int argc, char **argv, BenchConfig &config) {
    bool strategySpecified = false;

    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            benchUsage();
            return false;
        } else if (arg == "-n") {
            if (i + 1 >= argc) {
                benchUsage();
                return false;
            }
            std::string_view value = argv[++i];
            uint32_t parsed = 0;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || ptr != end || parsed == 0) {
                std::cerr << "invalid iteration count: " << value << "\n";
                benchUsage();
                return false;
            }
            config.iterations = parsed;
        } else if (arg == "--max-steps") {
            if (i + 1 >= argc) {
                benchUsage();
                return false;
            }
            std::string_view value = argv[++i];
            uint64_t parsed = 0;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || ptr != end) {
                std::cerr << "invalid --max-steps value: " << value << "\n";
                benchUsage();
                return false;
            }
            config.maxSteps = parsed;
        } else if (arg == "--table") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runTable = true;
        } else if (arg == "--switch") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runSwitch = true;
        } else if (arg == "--threaded") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runThreaded = true;
        } else if (arg == "--bc-switch") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runBytecodeSwitch = true;
        } else if (arg == "--bc-threaded") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runBytecodeThreaded = true;
        } else if (arg == "--bytecode") {
            beginExplicitStrategySelection(config, strategySpecified);
            config.runBytecodeSwitch = true;
            config.runBytecodeThreaded = true;
        } else if (arg == "--all") {
            config.runTable = true;
            config.runSwitch = true;
            config.runThreaded = true;
            config.runBytecodeSwitch = true;
            config.runBytecodeThreaded = true;
            strategySpecified = false;
        } else if (arg == "--json") {
            config.jsonOutput = true;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            benchUsage();
            return false;
        } else {
            config.ilFiles.push_back(std::string(arg));
        }
    }

    if (config.ilFiles.empty()) {
        std::cerr << "No input files specified\n";
        benchUsage();
        return false;
    }

    return true;
}

/// @brief Parse bench args, distinguishing a help request from a parse error.
/// @details Scans for --help/-h first (returning Help), otherwise delegates to
///          parseBenchArgs and maps its bool result to Ok/Error.
BenchParseResult parseBenchArgsChecked(int argc, char **argv, BenchConfig &config) {
    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            benchUsage();
            return BenchParseResult::Help;
        }
    }
    return parseBenchArgs(argc, argv, config) ? BenchParseResult::Ok : BenchParseResult::Error;
}

/// @brief Run a single benchmark iteration.
/// @param mod Module to execute.
/// @param strategy Dispatch strategy name ("table", "switch", "threaded").
/// @param maxSteps Maximum steps (0 = unlimited).
/// @return Benchmark result.
BenchResult runBenchmarkIteration(const core::Module &mod,
                                  const std::string &strategy,
                                  uint64_t maxSteps) {
    BenchResult result;
    result.strategy = strategy;

    // Set dispatch strategy via environment variable
    setenv("VIPER_DISPATCH", strategy.c_str(), 1);

    // Create Runner with minimal configuration
    vm::RunConfig runCfg;
    runCfg.maxSteps = maxSteps;

    auto start = std::chrono::steady_clock::now();

    try {
        vm::Runner runner(mod, std::move(runCfg));
        result.returnValue = runner.run();
        result.instructions = runner.instructionCount();
    } catch (const std::exception &) {
        result.success = false;
        return result;
    }

    auto end = std::chrono::steady_clock::now();
    result.timeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    if (result.timeMs > 0) {
        result.insnsPerSec = (result.instructions / result.timeMs) * 1000.0;
    }

    return result;
}

/// @brief Run a single bytecode VM benchmark iteration.
/// @param mod Module to execute.
/// @param bcModule Pre-compiled bytecode module.
/// @param strategy Dispatch strategy name ("bc-switch" or "bc-threaded").
/// @return Benchmark result.
BenchResult runBytecodeBenchmarkIteration(const core::Module &mod,
                                          const viper::bytecode::BytecodeModule &bcModule,
                                          const std::string &strategy,
                                          uint64_t maxSteps) {
    BenchResult result;
    result.strategy = strategy;

    bool useThreaded = (strategy == "bc-threaded");

    auto start = std::chrono::steady_clock::now();

    try {
        viper::bytecode::BytecodeVM vm;
        vm.setThreadedDispatch(useThreaded);
        vm.setRuntimeBridgeEnabled(true);
        vm.setMaxInstructions(maxSteps);
        vm.load(&bcModule);

        auto retSlot = vm.exec("main", {});
        result.returnValue = retSlot.i64;
        result.instructions = vm.instrCount();

        if (vm.state() == viper::bytecode::VMState::Trapped) {
            result.success = false;
            return result;
        }
    } catch (const std::exception &) {
        result.success = false;
        return result;
    }

    auto end = std::chrono::steady_clock::now();
    result.timeMs =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    if (result.timeMs > 0) {
        result.insnsPerSec = (result.instructions / result.timeMs) * 1000.0;
    }

    return result;
}

/// @brief Compute median of a vector of doubles.
double computeMedian(std::vector<double> values) {
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
                   std::vector<BenchResult> &results) {
    il::support::SourceManager sm;
    core::Module mod;

    auto load = il::tools::common::loadModuleFromFile(file, mod, std::cerr);
    if (!load.succeeded()) {
        std::cerr << "Failed to load: " << file << "\n";
        return false;
    }

    if (!il::tools::common::verifyModule(mod, std::cerr, &sm)) {
        std::cerr << "Verification failed: " << file << "\n";
        return false;
    }

    // Standard VM strategies
    std::vector<std::string> strategies;
    if (config.runTable)
        strategies.push_back("table");
    if (config.runSwitch)
        strategies.push_back("switch");
    if (config.runThreaded)
        strategies.push_back("threaded");

    for (const auto &strategy : strategies) {
        std::vector<double> times;
        std::vector<double> insnsPerSec;
        uint64_t instructions = 0;
        int64_t returnValue = 0;
        bool allSuccess = true;

        if (config.verbose) {
            std::cerr << "Running " << file << " with " << strategy << " (" << config.iterations
                      << " iterations)...\n";
        }

        for (uint32_t iter = 0; iter < config.iterations; ++iter) {
            auto iterResult = runBenchmarkIteration(mod, strategy, config.maxSteps);
            if (!iterResult.success) {
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

        if (allSuccess && !times.empty()) {
            result.timeMs = computeMedian(times);
            result.insnsPerSec = computeMedian(insnsPerSec);
        }

        results.push_back(result);
    }

    // Bytecode VM strategies
    std::vector<std::string> bcStrategies;
    if (config.runBytecodeSwitch)
        bcStrategies.push_back("bc-switch");
    if (config.runBytecodeThreaded)
        bcStrategies.push_back("bc-threaded");

    if (!bcStrategies.empty()) {
        // Compile IL to bytecode once
        viper::bytecode::BytecodeCompiler compiler;
        auto compiled = compiler.compileChecked(mod, &sm, true);
        if (!compiled) {
            il::support::printDiag(compiled.error(), std::cerr, &sm);
            return false;
        }
        viper::bytecode::BytecodeModule bcModule = std::move(compiled.value());

        for (const auto &strategy : bcStrategies) {
            std::vector<double> times;
            std::vector<double> insnsPerSec;
            uint64_t instructions = 0;
            int64_t returnValue = 0;
            bool allSuccess = true;

            if (config.verbose) {
                std::cerr << "Running " << file << " with " << strategy << " (" << config.iterations
                          << " iterations)...\n";
            }

            for (uint32_t iter = 0; iter < config.iterations; ++iter) {
                auto iterResult =
                    runBytecodeBenchmarkIteration(mod, bcModule, strategy, config.maxSteps);
                if (!iterResult.success) {
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

            if (allSuccess && !times.empty()) {
                result.timeMs = computeMedian(times);
                result.insnsPerSec = computeMedian(insnsPerSec);
            }

            results.push_back(result);
        }
    }

    return true;
}

/// @brief Print results in text format.
void printTextResults(const std::vector<BenchResult> &results) {
    for (const auto &r : results) {
        if (!r.success) {
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
void printJsonResults(const std::vector<BenchResult> &results) {
    auto escapeJson = [](std::string_view input) {
        std::string out;
        out.reserve(input.size() + 8);
        for (char c : input) {
            switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    } else {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    };

    std::cout << "[\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto &r = results[i];
        std::cout << "  {\n";
        std::cout << "    \"file\": \"" << escapeJson(r.file) << "\",\n";
        std::cout << "    \"strategy\": \"" << escapeJson(r.strategy) << "\",\n";
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
int cmdBench(int argc, char **argv) {
    BenchConfig config;
    switch (parseBenchArgsChecked(argc, argv, config)) {
        case BenchParseResult::Ok:
            break;
        case BenchParseResult::Help:
            return 0;
        case BenchParseResult::Error:
            return 1;
    }

    std::vector<BenchResult> allResults;

    bool hadFailure = false;
    for (const auto &file : config.ilFiles) {
        if (!benchmarkFile(file, config, allResults)) {
            hadFailure = true;
        }
    }

    if (allResults.empty()) {
        std::cerr << "No benchmark results\n";
        return 1;
    }

    if (config.jsonOutput) {
        printJsonResults(allResults);
    } else {
        printTextResults(allResults);
    }

    return hadFailure ? 1 : 0;
}
