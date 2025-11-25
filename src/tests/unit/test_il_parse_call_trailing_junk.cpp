//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_call_trailing_junk.cpp
// Purpose: Verify call operand parser rejects trailing tokens after the argument list.
// Key invariants: Parser emits a malformed call diagnostic when extra text follows ')'.
// Ownership/Lifetime: Test constructs modules and input buffers locally.
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
extern @foo() -> void
func @main() -> void {
entry:
  %x = call @foo() junk
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
    assert(message.find("malformed call") != std::string::npos);

    return 0;
}
