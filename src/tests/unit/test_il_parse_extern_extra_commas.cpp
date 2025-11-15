// File: tests/unit/test_il_parse_extern_extra_commas.cpp
// Purpose: Ensure extern declarations reject empty parameter slots separated by commas.
// Key invariants: Parser emits diagnostics for malformed extern parameter lists.
// Ownership/Lifetime: Test constructs modules and diagnostic buffers locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    const std::string source = R"(il 0.1.2
extern @foo(i64,, i64) -> i64
func @main() -> i64 {
entry:
  ret 0
})";

    std::istringstream in(source);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(!parsed);

    std::ostringstream diag;
    il::support::printDiag(parsed.error(), diag);
    const std::string message = diag.str();
    assert(message.find("malformed extern parameter") != std::string::npos);
    assert(message.find("empty entry") != std::string::npos);
    assert(message.find("line 2") != std::string::npos);

    return 0;
}
