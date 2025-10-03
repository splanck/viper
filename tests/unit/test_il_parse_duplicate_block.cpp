// File: tests/unit/test_il_parse_duplicate_block.cpp
// Purpose: Ensure parser rejects duplicate block labels within a function.
// Key invariants: Parsing fails with a "duplicate block" diagnostic referencing the label.
// Ownership/Lifetime: Test constructs modules and diagnostic buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <fstream>
#include <sstream>

#ifndef PARSE_ROUNDTRIP_DIR
#error "PARSE_ROUNDTRIP_DIR must be defined"
#endif

int main()
{
    const char *path = PARSE_ROUNDTRIP_DIR "/duplicate_block.il";
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    buf.seekg(0);

    il::core::Module m;
    auto parse = il::api::v2::parse_text_expected(buf, m);
    assert(!parse);

    std::ostringstream diag;
    il::support::printDiag(parse.error(), diag);
    const std::string message = diag.str();
    assert(message.find("duplicate block 'next'") != std::string::npos);
    assert(message.find("line 8") != std::string::npos);

    return 0;
}
