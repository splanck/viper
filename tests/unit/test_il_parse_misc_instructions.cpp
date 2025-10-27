// File: tests/unit/test_il_parse_misc_instructions.cpp
// Purpose: Exercise metadata-driven instruction parsing across varied opcode forms.
// Key invariants: Parser accepts operands/results/labels for uncommon opcodes.
// Ownership/Lifetime: Test owns all modules and buffers.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include <cassert>
#include <cmath>
#include <sstream>

int main()
{
    const char *src = R"(il 0.1.2
extern @foo(i64) -> i64
global const str @g = "hi"
func @main(%flag:i1) -> void {
entry(%flag:i1):
  %t0 = const_null
  %t1 = addr_of @g
  %t2 = const_str "hi"
  %t3 = alloca 8
  store i64, %t3, 42
  store i64, %t3, 0x2A
  store i64, %t3, 0b101010
  store i64, %t3, 0xFEED
  store i64, %t3, 0x1e
  %fbuf = alloca 8
  store f64, %fbuf, 1e1
  store f64, %fbuf, NaN
  store f64, %fbuf, INF
  store f64, %fbuf, -Inf
  %t4 = load i64, %t3
  %t5 = zext1 %flag
  %t6 = alloca 1
  store i1, %t6, true
  store i1, %t6, FALSE
  cbr %flag, true_bb(%t4), false_bb
true_bb(%x:i64):
  br exit(%x)
false_bb:
  %call = call @foo(%t4)
  trap
exit(%v:i64):
  ret %v
}
)";

    std::istringstream in(src);
    il::core::Module m;
    auto parse = il::api::v2::parse_text_expected(in, m);
    assert(parse);

    assert(m.externs.size() == 1);
    assert(m.externs[0].name == "foo");
    assert(m.globals.size() == 1);
    assert(m.globals[0].name == "g");
    assert(m.functions.size() == 1);

    const auto &fn = m.functions[0];
    assert(fn.blocks.size() == 4);

    const auto &entry = fn.blocks[0];
    assert(entry.instructions.size() == 20);

    const auto &constNull = entry.instructions[0];
    assert(constNull.op == il::core::Opcode::ConstNull);
    assert(constNull.type.kind == il::core::Type::Kind::Ptr);
    assert(constNull.operands.empty());

    const auto &addrOf = entry.instructions[1];
    assert(addrOf.op == il::core::Opcode::AddrOf);
    assert(addrOf.operands.size() == 1);
    assert(addrOf.operands[0].kind == il::core::Value::Kind::GlobalAddr);
    assert(addrOf.operands[0].str == "g");

    const auto &constStr = entry.instructions[2];
    assert(constStr.op == il::core::Opcode::ConstStr);
    assert(constStr.operands.size() == 1);
    assert(constStr.operands[0].kind == il::core::Value::Kind::ConstStr);
    assert(constStr.operands[0].str == "hi");

    const auto &allocaInstr = entry.instructions[3];
    assert(allocaInstr.op == il::core::Opcode::Alloca);
    assert(allocaInstr.operands.size() == 1);
    assert(allocaInstr.operands[0].kind == il::core::Value::Kind::ConstInt);
    assert(allocaInstr.operands[0].i64 == 8);
    assert(allocaInstr.type.kind == il::core::Type::Kind::Ptr);

    const auto &storeDecimal = entry.instructions[4];
    assert(storeDecimal.op == il::core::Opcode::Store);
    assert(storeDecimal.type.kind == il::core::Type::Kind::I64);
    assert(storeDecimal.operands.size() == 2);
    assert(storeDecimal.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeDecimal.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeDecimal.operands[1].i64 == 42);

    const auto &storeHex = entry.instructions[5];
    assert(storeHex.op == il::core::Opcode::Store);
    assert(storeHex.type.kind == il::core::Type::Kind::I64);
    assert(storeHex.operands.size() == 2);
    assert(storeHex.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeHex.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeHex.operands[1].i64 == 42);

    const auto &storeBinary = entry.instructions[6];
    assert(storeBinary.op == il::core::Opcode::Store);
    assert(storeBinary.type.kind == il::core::Type::Kind::I64);
    assert(storeBinary.operands.size() == 2);
    assert(storeBinary.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeBinary.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeBinary.operands[1].i64 == 42);

    const auto &storeHexFeed = entry.instructions[7];
    assert(storeHexFeed.op == il::core::Opcode::Store);
    assert(storeHexFeed.type.kind == il::core::Type::Kind::I64);
    assert(storeHexFeed.operands.size() == 2);
    assert(storeHexFeed.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeHexFeed.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeHexFeed.operands[1].i64 == 0xFEED);

    const auto &storeHex1e = entry.instructions[8];
    assert(storeHex1e.op == il::core::Opcode::Store);
    assert(storeHex1e.type.kind == il::core::Type::Kind::I64);
    assert(storeHex1e.operands.size() == 2);
    assert(storeHex1e.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeHex1e.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeHex1e.operands[1].i64 == 0x1e);

    const auto &floatAlloca = entry.instructions[9];
    assert(floatAlloca.op == il::core::Opcode::Alloca);
    assert(floatAlloca.operands.size() == 1);
    assert(floatAlloca.operands[0].kind == il::core::Value::Kind::ConstInt);
    assert(floatAlloca.operands[0].i64 == 8);

    const auto &storeFloat = entry.instructions[10];
    assert(storeFloat.op == il::core::Opcode::Store);
    assert(storeFloat.type.kind == il::core::Type::Kind::F64);
    assert(storeFloat.operands.size() == 2);
    assert(storeFloat.operands[0].kind == il::core::Value::Kind::Temp);
    assert(storeFloat.operands[1].kind == il::core::Value::Kind::ConstFloat);
    assert(storeFloat.operands[1].f64 == 10.0);

    const auto &storeNaN = entry.instructions[11];
    assert(storeNaN.op == il::core::Opcode::Store);
    assert(storeNaN.type.kind == il::core::Type::Kind::F64);
    assert(storeNaN.operands.size() == 2);
    assert(storeNaN.operands[1].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isnan(storeNaN.operands[1].f64));

    const auto &storeInf = entry.instructions[12];
    assert(storeInf.op == il::core::Opcode::Store);
    assert(storeInf.type.kind == il::core::Type::Kind::F64);
    assert(storeInf.operands.size() == 2);
    assert(storeInf.operands[1].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isinf(storeInf.operands[1].f64));
    assert(!std::signbit(storeInf.operands[1].f64));

    const auto &storeNegInf = entry.instructions[13];
    assert(storeNegInf.op == il::core::Opcode::Store);
    assert(storeNegInf.type.kind == il::core::Type::Kind::F64);
    assert(storeNegInf.operands.size() == 2);
    assert(storeNegInf.operands[1].kind == il::core::Value::Kind::ConstFloat);
    assert(std::isinf(storeNegInf.operands[1].f64));
    assert(std::signbit(storeNegInf.operands[1].f64));

    const auto &loadInstr = entry.instructions[14];
    assert(loadInstr.op == il::core::Opcode::Load);
    assert(loadInstr.type.kind == il::core::Type::Kind::I64);
    assert(loadInstr.operands.size() == 1);
    assert(loadInstr.operands[0].kind == il::core::Value::Kind::Temp);

    const auto &zextInstr = entry.instructions[15];
    assert(zextInstr.op == il::core::Opcode::Zext1);
    assert(zextInstr.operands.size() == 1);
    assert(zextInstr.operands[0].kind == il::core::Value::Kind::Temp);
    assert(zextInstr.type.kind == il::core::Type::Kind::I64);

    const auto &boolAlloca = entry.instructions[16];
    assert(boolAlloca.op == il::core::Opcode::Alloca);
    assert(boolAlloca.operands.size() == 1);
    assert(boolAlloca.operands[0].kind == il::core::Value::Kind::ConstInt);
    assert(boolAlloca.operands[0].i64 == 1);

    const auto &storeBoolTrue = entry.instructions[17];
    assert(storeBoolTrue.op == il::core::Opcode::Store);
    assert(storeBoolTrue.type.kind == il::core::Type::Kind::I1);
    assert(storeBoolTrue.operands.size() == 2);
    assert(storeBoolTrue.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeBoolTrue.operands[1].i64 == 1);
    assert(storeBoolTrue.operands[1].isBool);

    const auto &storeBoolFalse = entry.instructions[18];
    assert(storeBoolFalse.op == il::core::Opcode::Store);
    assert(storeBoolFalse.type.kind == il::core::Type::Kind::I1);
    assert(storeBoolFalse.operands.size() == 2);
    assert(storeBoolFalse.operands[1].kind == il::core::Value::Kind::ConstInt);
    assert(storeBoolFalse.operands[1].i64 == 0);
    assert(storeBoolFalse.operands[1].isBool);

    const auto &cbrInstr = entry.instructions[19];
    assert(cbrInstr.op == il::core::Opcode::CBr);
    assert(cbrInstr.operands.size() == 1);
    assert(cbrInstr.operands[0].kind == il::core::Value::Kind::Temp);
    assert(cbrInstr.labels.size() == 2);
    assert(cbrInstr.labels[0] == "true_bb");
    assert(cbrInstr.labels[1] == "false_bb");
    assert(cbrInstr.brArgs.size() == 2);
    assert(cbrInstr.brArgs[0].size() == 1);
    assert(cbrInstr.brArgs[0][0].kind == il::core::Value::Kind::Temp);
    assert(cbrInstr.brArgs[1].empty());

    const auto &trueBB = fn.blocks[1];
    assert(trueBB.instructions.size() == 1);
    const auto &brInstr = trueBB.instructions[0];
    assert(brInstr.op == il::core::Opcode::Br);
    assert(brInstr.labels.size() == 1);
    assert(brInstr.labels[0] == "exit");
    assert(brInstr.brArgs.size() == 1);
    assert(brInstr.brArgs[0].size() == 1);
    assert(brInstr.brArgs[0][0].kind == il::core::Value::Kind::Temp);

    const auto &falseBB = fn.blocks[2];
    assert(falseBB.instructions.size() == 2);
    const auto &callInstr = falseBB.instructions[0];
    assert(callInstr.op == il::core::Opcode::Call);
    assert(callInstr.callee == "foo");
    assert(callInstr.operands.size() == 1);
    assert(callInstr.operands[0].kind == il::core::Value::Kind::Temp);
    assert(callInstr.type.kind == il::core::Type::Kind::Void);
    const auto &trapInstr = falseBB.instructions[1];
    assert(trapInstr.op == il::core::Opcode::Trap);
    assert(trapInstr.operands.empty());

    const auto &exitBB = fn.blocks[3];
    assert(exitBB.instructions.size() == 1);
    const auto &retInstr = exitBB.instructions[0];
    assert(retInstr.op == il::core::Opcode::Ret);
    assert(retInstr.operands.size() == 1);
    assert(retInstr.operands[0].kind == il::core::Value::Kind::Temp);
    assert(retInstr.type.kind == il::core::Type::Kind::Void);

    return 0;
}
