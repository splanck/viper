// File: tests/unit/test_il_parse_trailing_comma.cpp
// Purpose: Ensure operand parsing rejects trailing commas in calls and branches.
// Key invariants: Parser emits malformed diagnostics referencing the offending context.
// Ownership/Lifetime: Test owns parser inputs and collects diagnostics locally.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"

#include <cassert>
#include <sstream>

int main()
{
    {
        const char *src = R"(il 0.1.2
extern @print(str) -> void
func @main() -> void {
entry:
  call @print("hello", )
  ret
}
)";

        std::istringstream in(src);
        il::core::Module module;
        auto parseResult = il::api::v2::parse_text_expected(in, module);
        assert(!parseResult);

        std::ostringstream diag;
        il::support::printDiag(parseResult.error(), diag);
        const std::string message = diag.str();
        assert(message.find("line 5") != std::string::npos);
        assert(message.find("malformed call") != std::string::npos);
    }

    {
        const char *src = R"(il 0.1.2
func @main() -> void {
entry:
  br ^dest(1, )
dest(%value:i32):
  ret
}
)";

        std::istringstream in(src);
        il::core::Module module;
        auto parseResult = il::api::v2::parse_text_expected(in, module);
        assert(!parseResult);

        std::ostringstream diag;
        il::support::printDiag(parseResult.error(), diag);
        const std::string message = diag.str();
        assert(message.find("line 4") != std::string::npos);
        assert(message.find("malformed br") != std::string::npos);
    }

    return 0;
}
