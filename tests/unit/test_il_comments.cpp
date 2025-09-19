// File: tests/unit/test_il_comments.cpp
// Purpose: Ensure IL parser handles files starting with comment headers.
// Key invariants: Leading lines beginning with '//' before the version line are ignored.
// Ownership/Lifetime: Test owns module and buffers locally.
// Links: docs/il-spec.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(// header line 1
// header line 2
il 0.1.2
func @main() -> i32 {
entry:
  ret 0
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
    assert(pe);
    assert(diag.str().empty());
    assert(m.functions.size() == 1);
    return 0;
}
