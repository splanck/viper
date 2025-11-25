//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_extra_param_commas.cpp
// Purpose: Ensure function headers reject empty parameter slots separated by commas.
// Key invariants: Parser reports detailed diagnostics for malformed parameter lists.
// Ownership/Lifetime: Test constructs modules and diagnostic buffers locally.
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
    const std::string source = R"(il 0.1.2
func @main(i32 %a,, i32 %b) -> i32 {
  ret %a
})";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(!parsed);

    std::ostringstream diag;
    il::support::printDiag(parsed.error(), diag);
    const std::string message = diag.str();
    assert(message.find("malformed parameter") != std::string::npos);
    assert(message.find("empty entry") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
