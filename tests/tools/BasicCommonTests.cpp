// File: tests/tools/BasicCommonTests.cpp
// Purpose: Exercise shared BASIC tool helpers for edge-case behaviours.
// Key invariants: loadBasicSource must leave buffers untouched on failure and surface errors.
// Ownership/Lifetime: Test manages temporary BASIC file and stream redirection.
// Links: src/tools/basic/common.cpp

#include "support/source_manager.hpp"
#include "tools/basic/common.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

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
    tmpPath /= "viper-basic-common-overflow-" + std::to_string(stamp) + ".bas";

    {
        std::ofstream ofs(tmpPath);
        ofs << "10 PRINT 1\n";
    }

    il::support::SourceManager sm;
    il::support::SourceManagerTestAccess::setNextFileId(
        sm,
        static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

    std::string buffer = "unchanged";
    std::string pathStr = tmpPath.string();

    std::ostringstream captured;
    auto *old = std::cerr.rdbuf(captured.rdbuf());

    std::optional<uint32_t> fileId = il::tools::basic::loadBasicSource(pathStr.c_str(), buffer, sm);

    std::cerr.flush();
    std::cerr.rdbuf(old);

    fs::remove(tmpPath);

    const std::string errText = captured.str();

    assert(!fileId.has_value());
    assert(buffer == "unchanged");
    assert(errText.find("cannot register") != std::string::npos);
    assert(errText.find(tmpPath.filename().string()) != std::string::npos);

    return 0;
}
