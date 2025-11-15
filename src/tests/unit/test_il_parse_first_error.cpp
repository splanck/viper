// File: tests/unit/test_il_parse_first_error.cpp
// Purpose: Ensure IL parser surfaces only the first diagnostic for malformed input.
// Key invariants: Parser stops after first fatal error and returns a single diagnostic payload.
// Ownership/Lifetime: Test owns all streams and module storage.
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
  %0 iadd.ovf 1, 2
  foo %1
}
)";
    std::istringstream in(src);
    il::core::Module m;
    auto parse = il::api::v2::parse_text_expected(in, m);
    assert(!parse);
    const std::string &message = parse.error().message;
    assert(message.find("missing '='") != std::string::npos);
    assert(message.find("unknown opcode") == std::string::npos);
    return 0;
}
