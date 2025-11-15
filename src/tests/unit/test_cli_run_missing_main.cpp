// File: tests/unit/test_cli_run_missing_main.cpp
// Purpose: Validate that cmdRunIL reports missing main without aborting.
// Key invariants: CLI must emit "missing main" and return non-zero.
// Ownership/Lifetime: Test owns temporary IL file it writes.
// Links: src/tools/ilc/cmd_run_il.cpp

#include "tools/ilc/cli.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char kModuleSource[] = "il 0.1\n\nfunc @helper() -> i64 {\nentry:\n  ret 0\n}\n";
}

// Stubbed usage() to satisfy linkage when embedding cmd_run_il.cpp in the test.
void usage() {}

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-ilc-missing-main-" + std::to_string(stamp) + ".il";
    {
        std::ofstream ofs(tmpPath);
        ofs << kModuleSource;
    }

    std::string pathStr = tmpPath.string();
    std::vector<char> argStorage(pathStr.begin(), pathStr.end());
    argStorage.push_back('\0');
    char *argv[] = {argStorage.data()};

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdRunIL(1, argv);
    std::cerr.flush();
    std::cerr.rdbuf(oldBuf);

    fs::remove(tmpPath);

    const std::string errText = errStream.str();
    const bool hasMessage = errText.find("missing main") != std::string::npos;

    assert(rc != 0);
    assert(hasMessage);
    return 0;
}
