//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_cli_run_source_manager_overflow.cpp
// Purpose: Ensure cmdRunIL aborts immediately when the SourceManager overflows.
// Key invariants: Overflow diagnostic is emitted and VM execution is skipped.
// Ownership/Lifetime: Test owns temporary IL file and diagnostic buffers.
// Links: src/tools/ilc/cmd_run_il.cpp
//
//===----------------------------------------------------------------------===//

#include "support/source_manager.hpp"
#include "tools/viper/cli.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Stub usage() required by cmd_run_il.cpp when embedded in the test binary.
void usage() {}

namespace il::support
{
struct SourceManagerTestAccess
{
    static void setNextFileId(SourceManager &sm, uint64_t next)
    {
        sm.next_file_id_ = next;
    }
};
} // namespace il::support

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-ilc-run-overflow-" + std::to_string(stamp) + ".il";

    {
        std::ofstream ofs(tmpPath);
        ofs << "il 0.1\n";
    }

    std::string pathStr = tmpPath.string();
    std::vector<char> argStorage(pathStr.begin(), pathStr.end());
    argStorage.push_back('\0');
    char *argv[] = {argStorage.data()};

    il::support::SourceManager sm;
    il::support::SourceManagerTestAccess::setNextFileId(
        sm, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

    std::ostringstream errStream;
    auto *oldErr = std::cerr.rdbuf(errStream.rdbuf());
    int rc = cmdRunILWithSourceManager(1, argv, sm);
    std::cerr.flush();
    std::cerr.rdbuf(oldErr);

    fs::remove(tmpPath);

    const std::string errText = errStream.str();
    const bool reportedOverflow =
        errText.find("source manager exhausted file identifier space") != std::string::npos;
    const bool reportedSummary = errText.find("[SUMMARY]") != std::string::npos;

    assert(rc != 0);
    assert(reportedOverflow);
    assert(!reportedSummary);
    return 0;
}
