// File: tests/unit/test_il_parse_global_missing_name.cpp
// Purpose: Verify the IL parser reports an error when a global name is omitted.
// Key invariants: Diagnostics surface missing identifier errors without crashing.
// Ownership/Lifetime: The test owns the module and parsing stream instances.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
global @ = "v"
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
    assert(message.find("missing global name") != std::string::npos);
    return 0;
}

