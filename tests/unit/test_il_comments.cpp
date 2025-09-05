// File: tests/unit/test_il_comments.cpp
// Purpose: Ensure IL parser skips comment-only lines.
// Key invariants: Parser succeeds when file begins with comments.
// Ownership/Lifetime: Test uses in-memory strings only.
// Links: docs/il-spec.md

#include "il/io/Parser.hpp"
#include <cassert>
#include <sstream>

int main()
{
    const char *src = "// File: dummy.il\n"
                      "// Purpose: test comment handling\n"
                      "il 0.1\n"
                      "\n"
                      "func @main() -> i64 {\n"
                      "entry:\n"
                      "  ret 0\n"
                      "}\n";
    il::core::Module m;
    std::stringstream buf(src);
    std::ostringstream err;
    bool ok = il::io::Parser::parse(buf, m, err);
    assert(ok);
    return 0;
}
