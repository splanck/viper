// File: tests/unit/test_il_parse_keyword_boundary.cpp
// Purpose: Ensure IL parser rejects identifiers where keywords are prefixes of longer tokens.
// Key invariants: Module parser must not treat 'func' as matching 'function'.
// Ownership/Lifetime: Test constructs parser inputs locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
function @main() -> void {
entry:
  ret
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    if (!parsed)
        il::support::printDiag(parsed.error(), diag);

    assert(!parsed);
    const std::string message = diag.str();
    assert(message.find("unexpected line: function") != std::string::npos);

    return 0;
}
