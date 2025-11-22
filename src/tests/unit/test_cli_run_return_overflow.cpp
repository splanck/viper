//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_cli_run_return_overflow.cpp
// Purpose: Ensure cmdRunIL reports an error when VM return value exceeds int range. 
// Key invariants: Overflow must emit diagnostic mentioning "outside host int range" and return
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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
const char kOverflowModule[] = "il 0.1\n\nfunc @main() -> i64 {\nentry:\n  ret 4294967296\n}\n";
}

// Stubbed usage() required by cmd_run_il.cpp.
void usage() {}

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-ilc-overflow-" + std::to_string(stamp) + ".il";
    {
        std::ofstream ofs(tmpPath);
        ofs << kOverflowModule;
    }

    std::string pathStr = tmpPath.string();
    std::vector<char> argStorage(pathStr.begin(), pathStr.end());
    argStorage.push_back('\0');
    char *argv[] = {argStorage.data()};

    std::ostringstream errStream;
    auto *oldBuf = std::cerr.rdbuf(errStream.rdbuf());
    const int rc = cmdRunIL(1, argv);
    std::cerr.flush();
    std::cerr.rdbuf(oldBuf);

    fs::remove(tmpPath);

    const std::string errText = errStream.str();
    const bool mentionedRange = errText.find("outside host int range") != std::string::npos;

    assert(rc != 0);
    assert(mentionedRange);
    return 0;
}
