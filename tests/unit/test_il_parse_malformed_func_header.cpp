// File: tests/unit/test_il_parse_malformed_func_header.cpp
// Purpose: Ensure parser rejects function headers missing delimiters.
// Key invariants: Parser sets hasError and returns false for malformed headers.
// Ownership/Lifetime: Test constructs modules and streams locally.
// Links: docs/il-spec.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
func @main() -> i32
)";
    std::istringstream in(src);
    il::core::Module m;
    std::ostringstream diag;
    auto pe = il::api::v2::parse_text_expected(in, m);
    if (!pe)
    {
        il::support::printDiag(pe.error(), diag);
    }
    assert(!pe);
    std::string msg = diag.str();
    assert(msg.find("malformed function header") != std::string::npos);
    return 0;
}
