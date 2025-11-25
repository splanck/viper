//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_duplicate_version.cpp
// Purpose: Ensure the IL parser rejects duplicate version directives in module headers.
// Key invariants: Parser reports a diagnostic referencing the second directive line.
// Ownership/Lifetime: Test owns module state and diagnostic streams local to main.
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
    const char *path = PARSE_ERROR_DIR "/duplicate_version.il";
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    buffer.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buffer, module);
    assert(!parseResult && "parser should reject duplicate 'il' directives");

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("duplicate 'il' version directive") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
