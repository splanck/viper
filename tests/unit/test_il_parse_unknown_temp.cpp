// File: tests/unit/test_il_parse_unknown_temp.cpp
// Purpose: Ensure IL parser reports an error when encountering an unknown SSA name.
// Key invariants: Parser surfaces diagnostics for unresolved temporary references.
// Ownership/Lifetime: Test constructs modules and diagnostic buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"

#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
func @main() -> i64 {
entry:
  %t0 = iadd.ovf %undef, 1
  ret 0
}
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
    std::string msg = diag.str();
    assert(msg.find("unknown temp '%undef'") != std::string::npos);
    return 0;
}
