//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/BenchOutputFormatTests.cpp
// Purpose: Validate that cmdBench produces correctly formatted output.
// Key invariants: Output must match "BENCH <file> <strategy> instr=... time_ms=...
// insns_per_sec=..." Ownership/Lifetime: Test owns temporary IL file it writes. Links:
// src/tools/ilc/cmd_bench.cpp
//
//===----------------------------------------------------------------------===//

#include "tools/viper/cli.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
// Simple IL module that returns 42
const char kModuleSource[] = R"(il 0.1

func @main() -> i64 {
entry:
  ret 42
}
)";
} // namespace

// Stubbed usage() to satisfy linkage when embedding cmd_bench.cpp in the test.
void usage() {}

int main() {
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-bench-test-" + std::to_string(stamp) + ".il";
    {
        std::ofstream ofs(tmpPath);
        ofs << kModuleSource;
    }

    std::string pathStr = tmpPath.string();

    // Build argv: <file.il> -n 1 --table
    std::vector<std::string> args = {pathStr, "-n", "1", "--table"};
    std::vector<char *> argv;
    for (auto &a : args) {
        argv.push_back(a.data());
    }

    // Capture stdout for output validation
    std::ostringstream outStream;
    auto *oldOutBuf = std::cout.rdbuf(outStream.rdbuf());

    int rc = cmdBench(static_cast<int>(argv.size()), argv.data());

    std::cout.flush();
    std::cout.rdbuf(oldOutBuf);

    fs::remove(tmpPath);

    const std::string outText = outStream.str();

    // Validate output format: "BENCH <file> table instr=<N> time_ms=<T> insns_per_sec=<R>"
    const std::regex benchRegex(R"(BENCH .+ table instr=\d+ time_ms=[\d.]+ insns_per_sec=\d+)");
    const bool formatMatches = std::regex_search(outText, benchRegex);

    // Exit code should be 0
    assert(rc == 0 && "cmdBench should return 0 on success");

    // Output should match expected format
    assert(formatMatches && "Output should match BENCH format");

    // --- Watchdog: a non-terminating IL program must abort on the step budget and skip the
    //     remaining strategies instead of hanging. A small explicit cap keeps the test fast. ---
    {
        static const char kInfiniteSource[] = R"(il 0.3.0

func @main() -> i64 {
entry:
  br loop

loop:
  br loop
}
)";
        const auto stamp2 = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path loopPath =
            fs::temp_directory_path() / ("viper-bench-loop-" + std::to_string(stamp2) + ".il");
        {
            std::ofstream ofs(loopPath);
            ofs << kInfiniteSource;
        }
        const std::string loopStr = loopPath.string();
        // Default strategies (table, switch, threaded); the cap bounds the run.
        std::vector<std::string> wargs = {loopStr, "-n", "1", "--max-steps", "1000"};
        std::vector<char *> wargv;
        for (auto &a : wargs)
            wargv.push_back(a.data());

        std::ostringstream wOut;
        std::ostringstream wErr;
        auto *savedOut = std::cout.rdbuf(wOut.rdbuf());
        auto *savedErr = std::cerr.rdbuf(wErr.rdbuf());
        (void)cmdBench(static_cast<int>(wargv.size()), wargv.data());
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(savedOut);
        std::cerr.rdbuf(savedErr);
        fs::remove(loopPath);

        const std::string wText = wOut.str() + wErr.str();
        assert(wText.find("step budget") != std::string::npos &&
               "bench watchdog should report the step budget on a non-terminating program");

        size_t benchCount = 0;
        for (size_t pos = wText.find("BENCH "); pos != std::string::npos;
             pos = wText.find("BENCH ", pos + 1))
            ++benchCount;
        assert(benchCount == 1 &&
               "bench should skip remaining strategies once the step budget is hit");
    }

    return 0;
}
