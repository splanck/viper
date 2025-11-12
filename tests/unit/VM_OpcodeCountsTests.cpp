// File: tests/unit/VM_OpcodeCountsTests.cpp
// Purpose: Verify opcode execution counters increment deterministically and honor runtime toggle.

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Opcode.hpp"
#include "viper/vm/VM.hpp"

#include <cassert>
#include <cstdint>
#include <numeric>

using namespace il::core;

static Module build_count_module()
{
    Module m;
    il::build::IRBuilder b(m);
    // main() -> i64: t0 = add 1,2; t1 = sub t0,1; t2 = mul t1,2; ret t2
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &entry = b.addBlock(mainFn, "entry");
    b.setInsertPoint(entry);

    unsigned t0 = b.reserveTempId();
    Instr add;
    add.result = t0;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(1));
    add.operands.push_back(Value::constInt(2));
    entry.instructions.push_back(add);

    unsigned t1 = b.reserveTempId();
    Instr sub;
    sub.result = t1;
    sub.op = Opcode::Sub;
    sub.type = Type(Type::Kind::I64);
    sub.operands.push_back(Value::temp(t0));
    sub.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(sub);

    unsigned t2 = b.reserveTempId();
    Instr mul;
    mul.result = t2;
    mul.op = Opcode::Mul;
    mul.type = Type(Type::Kind::I64);
    mul.operands.push_back(Value::temp(t1));
    mul.operands.push_back(Value::constInt(2));
    entry.instructions.push_back(mul);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(t2));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return m;
}

static uint64_t sumCounts(const std::array<uint64_t, il::core::kNumOpcodes> &arr)
{
    uint64_t s = 0;
    for (uint64_t v : arr)
        s += v;
    return s;
}

int main()
{
    Module m = build_count_module();

    // Scenario 1: counting enabled (default) -> known bins increment.
    {
        il::vm::VM vm(m);
        vm.resetOpcodeCounts();
        int64_t rc = vm.run();
        (void)rc;
        const auto &counts = vm.opcodeCounts();
        assert(counts[static_cast<size_t>(Opcode::Add)] == 1);
        assert(counts[static_cast<size_t>(Opcode::Sub)] == 1);
        assert(counts[static_cast<size_t>(Opcode::Mul)] == 1);
        assert(counts[static_cast<size_t>(Opcode::Ret)] == 1);
        assert(sumCounts(counts) == 4);
    }

    // Scenario 2: toggle off -> all zeros after run.
    {
        il::vm::VM vm(m);
        vm.resetOpcodeCounts();
#if VIPER_VM_OPCOUNTS
        il::vm::VMTestHook::setOpcodeCountsEnabled(vm, false);
#endif
        (void)vm.run();
        const auto &counts = vm.opcodeCounts();
        assert(sumCounts(counts) == 0);
    }

    return 0;
}
