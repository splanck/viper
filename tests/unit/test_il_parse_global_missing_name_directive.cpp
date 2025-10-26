// File: tests/unit/test_il_parse_global_missing_name_directive.cpp
// Purpose: Ensure IL module parser diagnoses globals without an identifier.
// Key invariants: Global directive must reject empty names between '@' and '='.
// Ownership/Lifetime: Test owns parser inputs, module instance, and diagnostics stream.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kSource = R"(il 0.1.2
global const str @ = "value"
)";

    std::istringstream input(kSource);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(!parsed);

    il::support::printDiag(parsed.error(), diag);
    const std::string message = diag.str();

    assert(message.find("missing global name") != std::string::npos);
    return 0;
}
