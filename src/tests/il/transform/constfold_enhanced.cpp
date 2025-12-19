//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/constfold_enhanced.cpp
// Purpose: Tests for enhanced constant folding including comparison folding,
//          shift operations, and proper boolean type production.
// Key invariants: Folding must preserve semantics and produce correct types.
// Ownership/Lifetime: Builds transient modules per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <iostream>

using namespace il::core;

namespace
{

static void verifyOrDie(const Module &module)
{
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
    {
        il::support::printDiag(verifyResult.error(), std::cerr);
        assert(false && "Module verification failed");
    }
}

void emitBinOp(BasicBlock &bb,
               Opcode op,
               Value lhs,
               Value rhs,
               unsigned resultId,
               Type ty = Type(Type::Kind::I64))
{
    Instr instr;
    instr.op = op;
    instr.result = resultId;
    instr.type = ty;
    instr.operands.push_back(lhs);
    instr.operands.push_back(rhs);
    bb.instructions.push_back(instr);
}

/// @brief Test that integer comparison folding produces boolean type.
void testIntegerComparisonFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_icmp", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %cmp = scmp_lt 5, 10 -> should fold to true (boolean)
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::SCmpLT, Value::constInt(5), Value::constInt(10), cmpId,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    // After folding, the ret should have a constant boolean operand
    const Instr &ret = entry.instructions.back();
    assert(ret.op == Opcode::Ret);
    assert(ret.operands.size() == 1);
    const Value &result = ret.operands[0];
    assert(result.kind == Value::Kind::ConstInt);
    assert(result.isBool && "Comparison result should be boolean");
    assert(result.i64 == 1 && "5 < 10 should be true");
}

/// @brief Test unsigned comparison folding.
void testUnsignedComparisonFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_ucmp", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %cmp = ucmp_gt 10, 5 -> should fold to true
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::UCmpGT, Value::constInt(10), Value::constInt(5), cmpId,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].isBool);
    assert(ret.operands[0].i64 == 1);
}

/// @brief Test shift operation folding.
void testShiftFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_shift", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %shl = shl 1, 4 -> should fold to 16
    unsigned shlId = builder.reserveTempId();
    emitBinOp(entry, Opcode::Shl, Value::constInt(1), Value::constInt(4), shlId);
    builder.emitRet(Value::temp(shlId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].i64 == 16 && "1 << 4 should equal 16");
}

/// @brief Test logical shift right folding.
void testLShrFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_lshr", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %lshr = lshr 256, 4 -> should fold to 16
    unsigned lshrId = builder.reserveTempId();
    emitBinOp(entry, Opcode::LShr, Value::constInt(256), Value::constInt(4), lshrId);
    builder.emitRet(Value::temp(lshrId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].i64 == 16 && "256 >> 4 should equal 16");
}

/// @brief Test float comparison folding produces boolean.
void testFloatComparisonFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_fcmp", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %cmp = fcmp.lt 1.0, 2.0 -> should fold to true (boolean)
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::FCmpLT, Value::constFloat(1.0), Value::constFloat(2.0), cmpId,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].isBool && "Float comparison should produce boolean");
    assert(ret.operands[0].i64 == 1 && "1.0 < 2.0 should be true");
}

/// @brief Test that equality comparison folds correctly.
void testEqualityFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_eq", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %cmp = icmp.eq 42, 42 -> should fold to true
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::ICmpEq, Value::constInt(42), Value::constInt(42), cmpId,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].isBool);
    assert(ret.operands[0].i64 == 1 && "42 == 42 should be true");
}

/// @brief Test inequality comparison folding.
void testInequalityFold()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_ne", Type(Type::Kind::I1), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %cmp = icmp.ne 1, 2 -> should fold to true
    unsigned cmpId = builder.reserveTempId();
    emitBinOp(entry, Opcode::ICmpNe, Value::constInt(1), Value::constInt(2), cmpId,
              Type(Type::Kind::I1));
    builder.emitRet(Value::temp(cmpId), {});

    verifyOrDie(module);

    il::transform::constFold(module);

    verifyOrDie(module);

    const Instr &ret = entry.instructions.back();
    assert(ret.operands[0].kind == Value::Kind::ConstInt);
    assert(ret.operands[0].isBool);
    assert(ret.operands[0].i64 == 1 && "1 != 2 should be true");
}

} // namespace

int main()
{
    testIntegerComparisonFold();
    testUnsignedComparisonFold();
    testShiftFold();
    testLShrFold();
    testFloatComparisonFold();
    testEqualityFold();
    testInequalityFold();
    return 0;
}
