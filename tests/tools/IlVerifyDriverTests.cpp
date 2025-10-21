// File: tests/tools/IlVerifyDriverTests.cpp
// Purpose: Validate il-verify pipeline handles SourceManager exhaustion early.
// Key invariants: Helper returns failure without invoking loader when file ids are exhausted.
// Ownership/Lifetime: Test owns the SourceManager and diagnostic buffers.
// Links: src/tools/il-verify/driver.cpp

#include "support/source_manager.hpp"
#include "tools/il-verify/driver.hpp"

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
        sm, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

    std::ostringstream out;
    std::ostringstream err;
    std::ostringstream diag;
    auto *oldBuf = std::cerr.rdbuf(diag.rdbuf());

    const bool ok = il::tools::verify::runVerificationPipeline("/tmp/missing.il", out, err, sm);

    std::cerr.rdbuf(oldBuf);

    if (ok)
    {
        return 1;
    }

    if (!out.str().empty())
    {
        return 1;
    }

    if (!err.str().empty())
    {
        return 1;
    }

    const std::string diagText = diag.str();
    if (diagText.find("source manager exhausted file identifier space") == std::string::npos)
    {
        return 1;
    }

    return 0;
}

