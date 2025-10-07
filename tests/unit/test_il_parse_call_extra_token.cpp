// File: tests/unit/test_il_parse_call_extra_token.cpp
// Purpose: Ensure call operands reject trailing tokens after the argument list.
// Key invariants: Parser emits the malformed call diagnostic with the correct line number.
// Ownership/Lifetime: Test owns the module and diagnostic buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2

func @main() -> void {
entry:
  call @foo() extra
  ret
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(input, module);
    assert(!parseResult);

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();

    assert(message.find("line 5") != std::string::npos);
    assert(message.find("malformed call") != std::string::npos);

    return 0;
}
