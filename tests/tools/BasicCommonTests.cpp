// File: tests/tools/BasicCommonTests.cpp
// Purpose: Validate shared BASIC tool helpers handle edge conditions gracefully.
// Key invariants: loadBasicSource must surface SourceManager failures and preserve buffers.
// Ownership/Lifetime: Test owns diagnostic streams and SourceManager instances.
// Links: docs/testing.md

#include "tools/basic/common.hpp"
#include "support/source_manager.hpp"

#include <filesystem>
#include <cstdint>
#include <limits>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
std::filesystem::path repoRoot()
{
    const auto sourcePath = std::filesystem::absolute(std::filesystem::path(__FILE__));
    return sourcePath.parent_path().parent_path().parent_path();
}
}

namespace il::support
{
struct SourceManagerTestAccess
{
    static void setNextFileId(SourceManager &sm, std::uint64_t next)
    {
        sm.next_file_id_ = next;
    }
};
} // namespace il::support

int main()
{
    const auto root = repoRoot();
    const auto basicPath = (root / "tests/data/mem2reg.bas").string();

    il::support::SourceManager sm;
    il::support::SourceManagerTestAccess::setNextFileId(
        sm,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1);

    std::string buffer = "preserve";
    std::ostringstream captured;
    auto *old = std::cerr.rdbuf(captured.rdbuf());

    auto result = il::tools::basic::loadBasicSource(basicPath.c_str(), buffer, sm);

    std::cerr.rdbuf(old);

    if (result.has_value())
        return 1;
    if (buffer != "preserve")
        return 1;

    const std::string diag = captured.str();
    if (diag.find("source manager exhausted file identifier space") == std::string::npos)
        return 1;
    if (diag.find("failed to register") == std::string::npos)
        return 1;

    return 0;
}
