// File: tests/unit/test_il_parse_malformed_func_header.cpp
// Purpose: Ensure parser rejects function headers missing delimiters.
// Key invariants: Parser reports malformed headers through Expected diagnostics.
// Ownership/Lifetime: Test constructs modules and streams locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include <cassert>
#include <sstream>
#include <string>

namespace
{

void expectMalformedHeader(const std::string &header)
{
    std::string src = "il 0.1.2\n" + header + "\n)";
    std::istringstream in(src);
    il::core::Module m;
    std::ostringstream diag;
    auto pe = il::api::v2::parse_text_expected(in, m);
    if (!pe)
        il::support::printDiag(pe.error(), diag);
    assert(!pe);
    std::string msg = diag.str();
    assert(msg.find("malformed function header") != std::string::npos);
}

} // namespace

int main()
{
    expectMalformedHeader("func @main() -> i64");
    expectMalformedHeader("func main() -> i64"); // Missing '@'
    expectMalformedHeader("func @main) -> i64"); // Missing '('
    expectMalformedHeader("func @main( -> i64"); // Missing ')'
    return 0;
}
