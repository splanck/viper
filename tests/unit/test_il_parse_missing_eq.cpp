// File: tests/unit/test_il_parse_missing_eq.cpp
// Purpose: Ensure IL parser reports error when result assignment lacks '='.
// Key invariants: Parser sets hasError and returns false for malformed instruction.
// Ownership/Lifetime: Test constructs modules and buffers locally.
// Links: docs/il-spec.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
func @main() -> i32 {
entry:
  %0 add 1, 2
}
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
    assert(msg.find("missing '='") != std::string::npos);
    return 0;
}
