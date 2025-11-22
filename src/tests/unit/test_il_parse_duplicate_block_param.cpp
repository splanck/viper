//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_duplicate_block_param.cpp
// Purpose: Ensure the IL parser rejects duplicate parameter names within a block header. 
// Key invariants: Parser emits a diagnostic mentioning the duplicate name and source line.
// Ownership/Lifetime: Test constructs module and diagnostic buffers locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <fstream>
#include <sstream>

#ifndef PARSE_ERROR_DIR
#error "PARSE_ERROR_DIR must be defined"
#endif

int main()
{
    const char *path = PARSE_ERROR_DIR "/duplicate_block_param.il";
    std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    buf.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buf, module);
    assert(!parseResult && "parser should reject duplicate block parameters");

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("duplicate parameter name '%x'") != std::string::npos);
    assert(message.find("line 3") != std::string::npos);

    return 0;
}
