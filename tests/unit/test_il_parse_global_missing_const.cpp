// File: tests/unit/test_il_parse_global_missing_const.cpp
// Purpose: Ensure IL parser diagnoses globals without the required const keyword.
// Key invariants: Diagnostic references the missing `const` marker.
// Ownership/Lifetime: Test owns module and parsing streams.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
global str @greeting = "hello"
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(!parsed);
    il::support::printDiag(parsed.error(), diag);

    const std::string message = diag.str();
    assert(message.find("missing 'const'") != std::string::npos);
    return 0;
}
