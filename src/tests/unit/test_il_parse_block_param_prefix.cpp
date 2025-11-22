//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_block_param_prefix.cpp
// Purpose: Ensure block headers reject parameters missing the '%' prefix. 
// Key invariants: Parser reports descriptive diagnostics for malformed block parameter names.
// Ownership/Lifetime: Test constructs modules and diagnostic buffers locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

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
    const char *path = PARSE_ROUNDTRIP_DIR "/block_param_missing_percent.il";
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    buffer.seekg(0);

    il::core::Module m;
    auto parse = il::api::v2::parse_text_expected(buffer, m);
    assert(!parse);

    std::ostringstream diag;
    il::support::printDiag(parse.error(), diag);
    const std::string message = diag.str();
    assert(message.find("parameter name must start with '%'") != std::string::npos);
    assert(message.find("line 5") != std::string::npos);

    return 0;
}
