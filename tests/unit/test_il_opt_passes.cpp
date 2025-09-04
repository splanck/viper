// File: tests/unit/test_il_opt_passes.cpp
// Purpose: Verify constfold and peephole passes preserve program semantics.
// Key invariants: Transformations reduce instructions without altering observable output.
// Ownership/Lifetime: N/A (test only).
// Links: docs/testing.md

#include "il/io/Parser.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/Peephole.hpp"
#include <cassert>
#include <iostream>
#include <sstream>

static il::core::Module parseModule(const char *src)
{
    std::stringstream ss(src);
    il::core::Module m;
    bool ok = il::io::Parser::parse(ss, m, std::cerr);
    assert(ok);
    return m;
}

int main()
{
    // ConstFold should fold constant arithmetic into literals.
    const char *cfSrc = R"(il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 1, 2
  call @rt_print_i64(%t0)
  ret 0
}
)";
    il::core::Module m1 = parseModule(cfSrc);
    il::transform::constFold(m1);
    assert(m1.functions.size() == 1);
    const auto &blk1 = m1.functions[0].blocks[0];
    assert(blk1.instructions.size() == 2);
    const auto &call1 = blk1.instructions[0];
    assert(call1.operands.size() == 1);
    assert(call1.operands[0].kind == il::core::Value::Kind::ConstInt);
    assert(call1.operands[0].i64 == 3);

    // Peephole should remove additions with an identity element.
    const char *phSrc = R"(il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 40, 2
  %t1 = add %t0, 0
  call @rt_print_i64(%t1)
  ret 0
}
)";
    il::core::Module m2 = parseModule(phSrc);
    il::transform::peephole(m2);
    assert(m2.functions.size() == 1);
    const auto &blk2 = m2.functions[0].blocks[0];
    assert(blk2.instructions.size() == 3);
    const auto &call2 = blk2.instructions[1];
    assert(call2.operands.size() == 1);
    // The add of 0 should be removed, leaving the original temporary.
    assert(call2.operands[0].kind == il::core::Value::Kind::Temp);

    return 0;
}
