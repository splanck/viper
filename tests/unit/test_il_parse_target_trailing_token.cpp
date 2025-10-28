// File: tests/unit/test_il_parse_target_trailing_token.cpp
// Purpose: Ensure the IL parser rejects trailing junk after target triples.
// Key invariants: Parser reports a diagnostic referencing the target directive line.
// Ownership/Lifetime: Test owns module state and diagnostic streams local to main.
// Links: docs/il-guide.md#reference

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
    const char *path = PARSE_ERROR_DIR "/target_trailing_token.il";
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    buffer.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buffer, module);
    assert(!parseResult && "parser should reject trailing target junk");

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("unexpected characters after target triple") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
