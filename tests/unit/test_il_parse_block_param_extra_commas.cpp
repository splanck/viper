// File: tests/unit/test_il_parse_block_param_extra_commas.cpp
// Purpose: Validate that block parameter lists reject empty entries between commas.
// Key invariants: Parser emits diagnostics referencing the empty slot and line number.
// Ownership/Lifetime: Test constructs module and diagnostic buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

#ifndef PARSE_ERROR_DIR
#error "PARSE_ERROR_DIR must be defined"
#endif

int main()
{
    const char *path = PARSE_ERROR_DIR "/block_param_extra_commas.il";
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    buffer.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buffer, module);
    assert(!parseResult && "parser should reject empty block parameter entries");

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("bad param") != std::string::npos);
    assert(message.find("empty entry") != std::string::npos);
    assert(message.find("line 3") != std::string::npos);

    return 0;
}
