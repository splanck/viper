// File: tests/tools/BasicCommonTests.cpp
// Purpose: Ensure BASIC tool helpers surface source manager failures.
// Key invariants: Buffer remains untouched on registration failures.
// Ownership/Lifetime: Test owns captured streams and buffers.
// Links: docs/testing.md

#include "support/source_manager.hpp"
#include "tools/basic/common.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
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
    il::support::SourceManager sm;
    il::support::SourceManagerTestAccess::setNextFileId(
        sm, static_cast<uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1);

    std::string buffer = "sentinel";
    std::stringstream captured;
    auto *old = std::cerr.rdbuf(captured.rdbuf());

    auto result = il::tools::basic::loadBasicSource(__FILE__, buffer, sm);

    std::cerr.rdbuf(old);

    assert(!result.has_value());
    assert(buffer == "sentinel");

    const std::string diagnostics = captured.str();
    assert(diagnostics.find("error:") != std::string::npos);
    assert(diagnostics.find("source manager exhausted file identifier space") != std::string::npos);
    assert(diagnostics.find("cannot register") != std::string::npos);

    return 0;
}
