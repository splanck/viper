//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/VM_MemWatchTests.cpp
// Purpose: Verify memory watch events fire when writes hit watched ranges.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/OpHandlerAccess.hpp"

#include <cassert>
#include <cstddef>

using namespace il::core;

static void build_store_program(Module &m, unsigned &ptrTemp)
{
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("main", Type(Type::Kind::Void), {});
    BasicBlock &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    // p = alloca 8
    Instr allocaI;
    ptrTemp = b.reserveTempId();
    allocaI.result = ptrTemp;
    allocaI.op = Opcode::Alloca;
    allocaI.type = Type(Type::Kind::Ptr);
    allocaI.operands.push_back(Value::constInt(8));
    bb.instructions.push_back(allocaI);

    // store.i64 p, 123
    Instr st;
    st.op = Opcode::Store;
    st.type = Type(Type::Kind::I64);
    st.operands.push_back(Value::temp(ptrTemp));
    st.operands.push_back(Value::constInt(123));
    bb.instructions.push_back(st);

    // ret
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    bb.instructions.push_back(ret);
    bb.terminated = true;
}

int main()
{
    Module m;
    unsigned ptrId = 0;
    build_store_program(m, ptrId);

    il::vm::VM vm(m);

    // Prepare execution and step once to execute alloca.
    auto &fn = m.functions.front();
    auto st = il::vm::VMTestHook::prepare(vm, fn);
    auto step1 = il::vm::VMTestHook::step(vm, st);
    (void)step1;

    // Retrieve the pointer address from the temporary and add a watch.
    assert(ptrId < st.fr.regs.size());
    void *p = st.fr.regs[ptrId].ptr;
    assert(p != nullptr);

    il::vm::DebugCtrl &dbg = il::vm::detail::VMAccess::debug(vm);
    dbg.addMemWatch(p, 8, "stack");

    // Execute the store; expect a watch-hit event.
    auto step2 = il::vm::VMTestHook::step(vm, st);
    (void)step2;
    auto hits = dbg.drainMemWatchEvents();
    assert(!hits.empty());
    bool hasStack = false;
    for (const auto &h : hits)
    {
        if (h.tag == "stack")
            hasStack = true;
    }
    assert(hasStack);

    return 0;
}
