//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_parse_global_missing_quotes.cpp
// Purpose: Ensure IL parser reports diagnostics when global string quotes are missing.
// Key invariants: Parser surfaces a single diagnostic instead of crashing on malformed globals.
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
    const char *src = R"(il 0.2.0
global const str @greeting = hello
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
    assert(message.find("missing opening '\"'") != std::string::npos ||
           message.find("missing closing '\"'") != std::string::npos);
    return 0;
}
