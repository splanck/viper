//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TrapDivideByZeroTests.cpp
// Purpose: Ensure DivideByZero traps report kind and instruction index.
// Key invariants: Diagnostic mentions DivideByZero and instruction #0 for the failing op.
// Ownership/Lifetime: Forks child VM process to capture trap output.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/TestIRBuilder.hpp"

#include <cassert>
#include <string>

using namespace il::core;

TEST_WITH_IL(il, {
    const auto lhs = il.const_i64(1);
    const auto rhs = il.const_i64(0);
    il.binary(Opcode::SDivChk0, Type(Type::Kind::I64), lhs, rhs, il.loc());
    il.retVoid(il.loc());

    const std::string out = il.captureTrap();
    const bool ok = out.find("Trap @main#0 line 1: DivideByZero (code=0)") != std::string::npos;
    assert(ok && "expected DivideByZero trap diagnostic with instruction index");
});
