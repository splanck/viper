// File: tests/unit/test_il_parse_global_missing_name.cpp
// Purpose: Ensure IL parser diagnoses globals referenced without a name.
// Key invariants: Operand parser must reject bare '@' operands with a clear diagnostic.
// Ownership/Lifetime: Test owns parser inputs and module state locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
func @main() -> void {
entry:
  %addr = addr_of @
  ret
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(!parsed);
    il::support::printDiag(parsed.error(), diag);

    const std::string message = diag.str();
    assert(message.find("missing global name") != std::string::npos);
    return 0;
}
