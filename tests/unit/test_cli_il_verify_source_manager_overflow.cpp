// File: tests/unit/test_cli_il_verify_source_manager_overflow.cpp
// Purpose: Ensure il-verify aborts when the SourceManager overflows before loading.
// Key invariants: Overflow diagnostic is emitted and module loading is skipped.
// Ownership/Lifetime: Test owns temporary IL file and captured stream buffers.
// Links: src/tools/il-verify/il-verify.cpp

#include "support/source_manager.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <ostream>
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
    tmpPath /= "viper-il-verify-overflow-" + std::to_string(stamp) + ".il";

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
        sm, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

    std::ostringstream outStream;
    std::ostringstream errStream;
    std::ostringstream capturedCerr;
    auto *oldCerr = std::cerr.rdbuf(capturedCerr.rdbuf());

    int rc = il::tools::verify::runCLI(2, argv, outStream, errStream, sm);

    std::cerr.rdbuf(oldCerr);

    fs::remove(tmpPath);

    const std::string errText = errStream.str();
    const std::string cerrText = capturedCerr.str();
    const std::string overflowMessage = "source manager exhausted file identifier space";
    const std::size_t firstPos = cerrText.find(overflowMessage);
    const bool reportedOverflow =
        firstPos != std::string::npos || errText.find(overflowMessage) != std::string::npos;
    const bool printedTwice =
        firstPos != std::string::npos &&
        cerrText.find(overflowMessage, firstPos + overflowMessage.size()) != std::string::npos;

    assert(rc != 0);
    assert(reportedOverflow);
    assert(!printedTwice);
    assert(errText.empty());
    assert(errText.find(overflowMessage) == std::string::npos);
    assert(outStream.str().find("OK") == std::string::npos);

    return 0;
}
