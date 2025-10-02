// File: tests/unit/test_il_parse_missing_brace.cpp
// Purpose: Ensure IL parser reports a diagnostic when a function body misses a closing brace.
// Key invariants: Parser surfaces EOF diagnostics referencing the final line number.
// Ownership/Lifetime: Test owns module buffers and diagnostics locally.
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
    const char *path = PARSE_ROUNDTRIP_DIR "/missing_brace.il";
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    buffer.seekg(0);

    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(buffer, module);
    assert(!parseResult);

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("line 5") != std::string::npos);
    assert(message.find("unexpected end of file") != std::string::npos);
    assert(message.find("missing '}'") != std::string::npos);

    return 0;
}
