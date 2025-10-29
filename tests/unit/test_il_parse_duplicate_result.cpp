// File: tests/unit/test_il_parse_duplicate_result.cpp
// Purpose: Ensure the IL parser rejects duplicate SSA result names within a block.
// Key invariants: Parser reports diagnostics when a result name is redefined.
// Ownership/Lifetime: Test owns module and diagnostic buffers constructed from string literals.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <sstream>

int main()
{
    static constexpr const char *kSource = R"(il 0.1.2
func @dup_result() -> void {
entry:
  %x = const_null
  %x = const_null
  ret
}
)";

    std::istringstream input(kSource);
    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(input, module);
    assert(!parseResult && "parser should reject duplicate result names");

    std::ostringstream diag;
    il::support::printDiag(parseResult.error(), diag);
    const std::string message = diag.str();
    assert(message.find("duplicate result name '%x'") != std::string::npos);
    assert(message.find("line 5") != std::string::npos);

    return 0;
}
