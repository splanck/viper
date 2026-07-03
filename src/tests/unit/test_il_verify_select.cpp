//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_select.cpp
// Purpose: Ensure Verifier enforces the select type rule: cond is i1 and both
//          arms match the instruction type.
// Key invariants: Well-typed selects verify; a mismatched arm or non-i1
//                 condition is rejected.
// Ownership/Lifetime: Parses transient modules per case.
// Links: docs/adr/0063-il-select-and-if-conversion.md
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"
#include <cassert>
#include <sstream>
#include <string>

namespace {

bool verifies(const std::string &text) {
    il::core::Module module;
    std::istringstream input(text);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);
    auto verified = il::verify::Verifier::verify(module);
    return static_cast<bool>(verified);
}

} // namespace

int main() {
    // Well-typed i64 and f64 selects pass.
    assert(verifies(R"(il 0.3.0
func @ok(%c: i1, %a: i64, %b: i64, %x: f64, %y: f64) -> i64 {
entry(%c: i1, %a: i64, %b: i64, %x: f64, %y: f64):
  %t0 = select i64, %c, %a, %b
  %t1 = select f64, %c, %x, %y
  ret %t0
}
)"));

    // Arm type mismatch: f64 arm on an i64 select is rejected.
    assert(!verifies(R"(il 0.3.0
func @bad_arm(%c: i1, %a: i64, %x: f64) -> i64 {
entry(%c: i1, %a: i64, %x: f64):
  %t0 = select i64, %c, %a, %x
  ret %t0
}
)"));

    // Condition must be i1: an i64 condition is rejected.
    assert(!verifies(R"(il 0.3.0
func @bad_cond(%c: i64, %a: i64, %b: i64) -> i64 {
entry(%c: i64, %a: i64, %b: i64):
  %t0 = select i64, %c, %a, %b
  ret %t0
}
)"));

    return 0;
}
