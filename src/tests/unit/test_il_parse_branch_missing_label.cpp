//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_branch_missing_label.cpp
// Purpose: Ensure parser reports a clear diagnostic when branch targets omit labels.
// Key invariants: Parser must detect and describe malformed branch targets before argument parsing.
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
    constexpr const char *kProgram = R"(il 0.2.0
func @main() -> void {
entry:
  br label ^("arg")
}
)";

    std::istringstream input(kProgram);
    il::core::Module module;
    std::ostringstream diag;

    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(!parsed);
    il::support::printDiag(parsed.error(), diag);

    const std::string message = diag.str();
    assert(message.find("malformed branch target") != std::string::npos);
    assert(message.find("missing label") != std::string::npos);

    return 0;
}
