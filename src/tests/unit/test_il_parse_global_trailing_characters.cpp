//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_global_trailing_characters.cpp
// Purpose: Ensure IL parser rejects globals that include trailing characters after string literals.
// Key invariants: Parser emits a diagnostic referencing unexpected trailing characters.
// Ownership/Lifetime: Test owns parsing streams and module instance.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
global const str @greeting = "hello" junk
)";
    std::istringstream in(src);
    il::core::Module m;
    std::ostringstream diag;
    auto parse = il::api::v2::parse_text_expected(in, m);
    if (!parse)
    {
        il::support::printDiag(parse.error(), diag);
    }
    assert(!parse);
    const std::string message = diag.str();
    assert(message.find("unexpected characters after closing '\"'") != std::string::npos);
    return 0;
}
