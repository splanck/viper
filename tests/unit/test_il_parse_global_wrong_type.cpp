// File: tests/unit/test_il_parse_global_wrong_type.cpp
// Purpose: Ensure IL parser rejects globals declared with unsupported types.
// Key invariants: Diagnostic names the offending type token.
// Ownership/Lifetime: Test owns module and parsing streams.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
global const i64 @counter = "0"
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(!parsed);
    il::support::printDiag(parsed.error(), diag);

    const std::string message = diag.str();
    assert(message.find("unsupported global type 'i64'") != std::string::npos);
    return 0;
}
