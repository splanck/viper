//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_missing_extern_name.cpp
// Purpose: Ensure extern declarations without a name produce diagnostics.
// Key invariants: Parser reports the missing extern name with the source line.
// Ownership/Lifetime: Test owns module state and diagnostic buffers.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    const std::string source = R"(il 0.2.0
extern @(i32) -> void
func @main() -> i64 {
entry:
  ret 0
})";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(!parsed);

    std::ostringstream diag;
    il::support::printDiag(parsed.error(), diag);
    const std::string message = diag.str();
    assert(message.find("missing extern name") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
