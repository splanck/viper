// File: tests/unit/test_il_parse_extern_missing_arrow.cpp
// Purpose: Ensure IL parser reports error when extern declaration lacks '->'.
// Key invariants: Parser sets hasError and returns false for malformed extern.
// Ownership/Lifetime: Test constructs modules and buffers locally.
// Links: docs/il-spec.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
extern @foo(i32)
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
    assert(!pe);
    std::string msg = diag.str();
    assert(msg.find("missing '->'") != std::string::npos);
    return 0;
}
