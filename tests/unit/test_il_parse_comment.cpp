// File: tests/unit/test_il_parse_comment.cpp
// Purpose: Ensure IL parser ignores comment lines.
// Key invariants: Parser treats lines starting with '//' as comments.
// Ownership/Lifetime: Test owns modules and buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
# hash comment before function
   # hash comment with leading spaces
// slash comment before function
func @main() -> i64 {
entry:
  # hash comment inside block
  // slash comment inside block
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
    assert(m.functions.front().blocks.size() == 1);
    assert(m.functions.front().blocks.front().instructions.size() == 1);
    return 0;
}
