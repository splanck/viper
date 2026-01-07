//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_missing_operand_between_commas.cpp
// Purpose: Ensure parser reports an error when operands include empty tokens.
// Key invariants: Parser must diagnose consecutive commas as missing operands.
// Ownership/Lifetime: Test owns parser inputs and module state locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    constexpr const char *kProgram = R"(il 0.1.2
func @main() -> void {
entry:
  %0 = add 1 , , 2
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
    assert(message.find("missing operand") != std::string::npos);
    assert(message.find("Line 4") != std::string::npos ||
           message.find("line 4") != std::string::npos);

    return 0;
}
