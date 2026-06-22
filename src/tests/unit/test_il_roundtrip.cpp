//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_roundtrip.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/verify/Verifier.hpp"
#include "viper/il/IO.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static void assertRoundTrip(const std::string &text) {
    std::istringstream first(text);
    il::core::Module m1;
    auto parse1 = il::api::v2::parse_text_expected(first, m1);
    assert(parse1);
    assert(il::verify::Verifier::verify(m1).hasValue());

    std::string s1 = il::io::Serializer::toString(m1);
    std::istringstream second(s1);
    il::core::Module m2;
    auto parse2 = il::api::v2::parse_text_expected(second, m2);
    assert(parse2);
    assert(il::verify::Verifier::verify(m2).hasValue());

    std::string s2 = il::io::Serializer::toString(m2);
    if (!s1.empty() && s1.back() == '\n')
        s1.pop_back();
    if (!s2.empty() && s2.back() == '\n')
        s2.pop_back();
    assert(s1 == s2);
}

// Return the first ConstFloat operand value found in a parsed module.
static double firstConstFloat(const std::string &text) {
    std::istringstream in(text);
    il::core::Module m;
    auto parsed = il::api::v2::parse_text_expected(in, m);
    assert(parsed);
    for (const auto &fn : m.functions)
        for (const auto &bb : fn.blocks)
            for (const auto &ins : bb.instructions)
                for (const auto &op : ins.operands)
                    if (op.kind == il::core::Value::Kind::ConstFloat)
                        return op.f64;
    assert(false && "no f64 constant found in module");
    return 0.0;
}

// Verify the IL serializer formats f64 constants so they re-parse bit-for-bit. The
// text-stability checks elsewhere can be self-consistently lossy; this asserts the
// stronger property that a serialized constant recovers the exact IEEE-754 bit pattern.
static void assertF64BitExact(double d) {
    char literal[64];
    std::snprintf(literal, sizeof literal, "%.17g", d); // round-trippable injection form
    // Carry the constant as the first fadd operand (the proven literal-operand idiom);
    // firstConstFloat returns that operand value, unaffected by the +0.0.
    const std::string text = std::string("il 0.2.0\nfunc @f() -> f64 {\nentry:\n  %r = fadd ") +
                             literal + ", 0.0\n  ret %r\n}\n";
    const double parsed = firstConstFloat(text);

    std::istringstream in(text);
    il::core::Module m;
    auto ok = il::api::v2::parse_text_expected(in, m);
    assert(ok);
    const std::string serialized = il::io::Serializer::toString(m);
    const double reparsed = firstConstFloat(serialized);

    std::uint64_t a = 0, b = 0;
    std::memcpy(&a, &parsed, sizeof a);
    std::memcpy(&b, &reparsed, sizeof b);
    assert(a == b && "f64 constant lost precision through serialize/parse");
}

int main() {
    // f64 serialize/parse must be bit-exact across the tricky-value spectrum: common
    // non-terminating decimals, transcendentals, the largest/smallest normals, and
    // large/small magnitudes. (Subnormals are excluded — the IL text parser does not
    // currently accept subnormal f64 literals, a separate parser limitation.)
    for (double d : {0.1, 1.0 / 3.0, 3.141592653589793, 2.718281828459045,
                     1.7976931348623157e308, 2.2250738585072014e-308, 123456789.123456789,
                     0.000123456789}) {
        assertF64BitExact(d);
    }

    const char *files[] = {EXAMPLES_DIR "/il/ex1_hello_cond.il",
                           EXAMPLES_DIR "/il/ex2_sum_1_to_10.il",
                           EXAMPLES_DIR "/il/ex3_table_5x5.il",
                           EXAMPLES_DIR "/il/ex4_factorial.il",
                           EXAMPLES_DIR "/il/ex5_strings.il",
                           EXAMPLES_DIR "/il/ex6_heap_array_avg.il",
                           ROUNDTRIP_DIR "/block-params.il",
                           ROUNDTRIP_DIR "/zero-args-shorthand.il"};
    for (const char *path : files) {
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        buf.seekg(0);
        il::core::Module m1;
        auto parse1 = il::api::v2::parse_text_expected(buf, m1);
        assert(parse1);
        std::string s1 = il::io::Serializer::toString(m1);
        std::istringstream in2(s1);
        il::core::Module m2;
        auto parse2 = il::api::v2::parse_text_expected(in2, m2);
        assert(parse2);
        std::string s2 = il::io::Serializer::toString(m2);
        if (!s1.empty() && s1.back() == '\n')
            s1.pop_back();
        if (!s2.empty() && s2.back() == '\n')
            s2.pop_back();
        assert(s1 == s2);
    }
    assertRoundTrip(R"(il 0.2.0
func @explicit_param_forward(%t40:i1) -> i64 {
entry(%t40:i1):
  cbr %t40, carrier(42), exit
use:
  ret %t48
carrier(%t48:i64):
  br use
exit:
  ret 0
}
func @explicit_result_forward(%flag:i1) -> i64 {
entry(%flag:i1):
  cbr %flag, preheader, exit
body:
  ret %t90
preheader:
  %t90 = iadd.ovf 40, 2
  br body
exit:
  ret 0
}
)");
    return 0;
}
