// File: tests/unit/test_il_parse_float_specials.cpp
// Purpose: Verify the IL parser accepts special floating-point literals.
// Key invariants: NaN and infinity tokens parse case-insensitively and map to IEEE values.
// Ownership/Lifetime: Test owns parsed module memory during execution.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <cmath>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
extern @rt_print_f64(f64) -> void
func @main() -> void {
entry:
  call @rt_print_f64(NaN)
  call @rt_print_f64(Inf)
  call @rt_print_f64(+Inf)
  call @rt_print_f64(-Inf)
  ret
}
)";

    std::istringstream in(src);
    il::core::Module module;
    auto parse = il::api::v2::parse_text_expected(in, module);
    assert(parse);

    assert(module.functions.size() == 1);
    const auto &fn = module.functions[0];
    assert(fn.blocks.size() == 1);
    const auto &entry = fn.blocks[0];
    assert(entry.instructions.size() == 5);

    const auto &nanCall = entry.instructions[0];
    assert(nanCall.operands.size() == 1);
    assert(nanCall.operands[0].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isnan(nanCall.operands[0].f64));

    const auto &infCall = entry.instructions[1];
    assert(infCall.operands.size() == 1);
    assert(infCall.operands[0].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isinf(infCall.operands[0].f64));
    assert(!std::signbit(infCall.operands[0].f64));

    const auto &posInfCall = entry.instructions[2];
    assert(posInfCall.operands.size() == 1);
    assert(posInfCall.operands[0].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isinf(posInfCall.operands[0].f64));
    assert(!std::signbit(posInfCall.operands[0].f64));

    const auto &negInfCall = entry.instructions[3];
    assert(negInfCall.operands.size() == 1);
    assert(negInfCall.operands[0].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isinf(negInfCall.operands[0].f64));
    assert(std::signbit(negInfCall.operands[0].f64));

    return 0;
}
