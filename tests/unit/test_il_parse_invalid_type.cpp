// File: tests/unit/test_il_parse_invalid_type.cpp
// Purpose: Ensure parser rejects extern declarations with unknown types.
// Key invariants: Parser returns false and reports unknown type diagnostic.
// Ownership/Lifetime: Test constructs modules and streams locally.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
extern @foo(i32) -> i64
)";
    std::istringstream in(src);
    il::core::Module m;
    std::ostringstream err;
    bool ok = il::io::Parser::parse(in, m, err);
    assert(!ok);
    std::string msg = err.str();
    assert(msg.find("unknown type") != std::string::npos);
    return 0;
}
