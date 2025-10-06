// File: tests/unit/test_il_parse_function_name_trim.cpp
// Purpose: Ensure function headers trim trailing whitespace from symbol names.
// Key invariants: Parser normalises function identifiers; verifier resolves calls.
// Ownership/Lifetime: Test owns module buffers parsed from string literals.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <sstream>

int main()
{
    static constexpr const char *kSource = R"(il 0.1.2
func @caller() -> void {
entry:
  call @callee()
  ret
}

func @callee   () -> void {
entry:
  ret
}
)";

    std::istringstream input(kSource);
    il::core::Module module;
    auto parseResult = il::api::v2::parse_text_expected(input, module);
    assert(parseResult && "parser should accept headers with trailing spaces");
    assert(module.functions.size() == 2);

    const auto &callee = module.functions.back();
    assert(callee.name == "callee" && "function name should be trimmed");

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "verifier should resolve calls to trimmed names");

    return 0;
}
