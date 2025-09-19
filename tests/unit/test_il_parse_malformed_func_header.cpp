// File: tests/unit/test_il_parse_malformed_func_header.cpp
// Purpose: Ensure parser rejects function headers missing delimiters.
// Key invariants: Parser sets hasError and returns false for malformed headers.
// Ownership/Lifetime: Test constructs modules and streams locally.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
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
    std::ostringstream err;
    bool ok = il::io::Parser::parse(in, m, err);
    assert(!ok);
    std::string msg = err.str();
    assert(msg.find("malformed function header") != std::string::npos);
    return 0;
}
