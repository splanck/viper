//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/tools/IlVerifyOverflowDiagOnceTests.cpp
// Purpose: Ensure il-verify reports SourceManager overflow exactly once. 
// Key invariants: Overflow diagnostics are emitted a single time to stderr.
// Ownership/Lifetime: Test owns temporary file and stream capture buffers.
// Links: src/tools/il-verify/il-verify.cpp
//
//===----------------------------------------------------------------------===//

#include "support/source_manager.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

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

namespace il::tools::verify
{
int runCLI(
    int argc, char **argv, std::ostream &out, std::ostream &err, il::support::SourceManager &sm);
} // namespace il::tools::verify

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tmpPath = fs::temp_directory_path();
    tmpPath /= "viper-il-verify-overflow-once-" + std::to_string(stamp) + ".il";

    {
        std::ofstream ofs(tmpPath);
        ofs << "il 0.1\n";
    }

    std::string pathStr = tmpPath.string();
    std::vector<char> pathStorage(pathStr.begin(), pathStr.end());
    pathStorage.push_back('\0');

    char exeName[] = "il-verify";
    char *argv[] = {exeName, pathStorage.data()};

    il::support::SourceManager sm;
    il::support::SourceManagerTestAccess::setNextFileId(
        sm, static_cast<uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1);

    std::ostringstream outStream;
    std::ostringstream errStream;
    std::ostringstream capturedCerr;
    auto *oldCerr = std::cerr.rdbuf(capturedCerr.rdbuf());

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    std::cerr.rdbuf(oldCerr);
    fs::remove(tmpPath);

    const std::string overflowMessage = "source manager exhausted file identifier space";
    const std::string captured = capturedCerr.str();
    const std::size_t firstPos = captured.find(overflowMessage);
    const std::size_t secondPos =
        firstPos != std::string::npos
            ? captured.find(overflowMessage, firstPos + overflowMessage.size())
            : std::string::npos;

    assert(rc != 0);
    assert(outStream.str().empty());
    assert(errStream.str().empty());
    assert(firstPos != std::string::npos);
    assert(secondPos == std::string::npos);

    return 0;
}
