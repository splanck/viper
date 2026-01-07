//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/ThreadsRuntimeTests.cpp
// Purpose: Validate VM-side Viper.Threads integration (notably Thread.Start override).
// Key invariants: VM Thread.Start accepts IL function pointers and shares module globals.
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>

int main()
{
#if defined(_WIN32)
    // Viper.Threads is currently not implemented for Windows targets.
    return 0;
#else
    using namespace il::core;

    Module m;
    il::build::IRBuilder b(m);

    b.addGlobal("g", Type(Type::Kind::I64));

    // Runtime externs (canonical names).
    b.addExtern("Viper.Threads.Thread.Start",
                Type(Type::Kind::Ptr),
                {Type(Type::Kind::Ptr), Type(Type::Kind::Ptr)});
    b.addExtern("Viper.Threads.Thread.Join", Type(Type::Kind::Void), {Type(Type::Kind::Ptr)});

    // worker() -> void: g = g + 1
    auto &worker = b.startFunction("worker", Type(Type::Kind::Void), {});
    auto &workerEntry = b.addBlock(worker, "entry");
    b.setInsertPoint(workerEntry);

    Instr gaddrPtr;
    gaddrPtr.result = b.reserveTempId();
    gaddrPtr.op = Opcode::GAddr;
    gaddrPtr.type = Type(Type::Kind::Ptr);
    gaddrPtr.operands.push_back(Value::global("g"));
    gaddrPtr.loc = {1, 1, 1};
    workerEntry.instructions.push_back(gaddrPtr);
    const unsigned gptrId = *gaddrPtr.result;

    Instr loadG;
    loadG.result = b.reserveTempId();
    loadG.op = Opcode::Load;
    loadG.type = Type(Type::Kind::I64);
    loadG.operands.push_back(Value::temp(gptrId));
    loadG.loc = {1, 1, 2};
    workerEntry.instructions.push_back(loadG);
    const unsigned gvalId = *loadG.result;

    Instr add1;
    add1.result = b.reserveTempId();
    add1.op = Opcode::Add;
    add1.type = Type(Type::Kind::I64);
    add1.operands.push_back(Value::temp(gvalId));
    add1.operands.push_back(Value::constInt(1));
    add1.loc = {1, 1, 3};
    workerEntry.instructions.push_back(add1);
    const unsigned gnextId = *add1.result;

    Instr storeG;
    storeG.op = Opcode::Store;
    storeG.type = Type(Type::Kind::I64);
    storeG.operands.push_back(Value::temp(gptrId));
    storeG.operands.push_back(Value::temp(gnextId));
    storeG.loc = {1, 1, 4};
    workerEntry.instructions.push_back(storeG);

    b.emitRet(std::optional<Value>{}, {1, 1, 5});

    // main() -> i64
    auto &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &mainEntry = b.addBlock(mainFn, "entry");
    b.setInsertPoint(mainEntry);

    // %gptr = gaddr @g; store 41
    Instr mainGPtr;
    mainGPtr.result = b.reserveTempId();
    mainGPtr.op = Opcode::GAddr;
    mainGPtr.type = Type(Type::Kind::Ptr);
    mainGPtr.operands.push_back(Value::global("g"));
    mainGPtr.loc = {1, 2, 1};
    mainEntry.instructions.push_back(mainGPtr);
    const unsigned mainGPtrId = *mainGPtr.result;

    Instr storeInit;
    storeInit.op = Opcode::Store;
    storeInit.type = Type(Type::Kind::I64);
    storeInit.operands.push_back(Value::temp(mainGPtrId));
    storeInit.operands.push_back(Value::constInt(41));
    storeInit.loc = {1, 2, 2};
    mainEntry.instructions.push_back(storeInit);

    // %entry = gaddr @worker  (VM represents function pointers as il::core::Function*).
    Instr entryPtr;
    entryPtr.result = b.reserveTempId();
    entryPtr.op = Opcode::GAddr;
    entryPtr.type = Type(Type::Kind::Ptr);
    entryPtr.operands.push_back(Value::global("worker"));
    entryPtr.loc = {1, 2, 3};
    mainEntry.instructions.push_back(entryPtr);
    const unsigned entryId = *entryPtr.result;

    Instr nullArg;
    nullArg.result = b.reserveTempId();
    nullArg.op = Opcode::ConstNull;
    nullArg.type = Type(Type::Kind::Ptr);
    nullArg.loc = {1, 2, 4};
    mainEntry.instructions.push_back(nullArg);
    const unsigned nullId = *nullArg.result;

    unsigned threadId = b.reserveTempId();
    b.emitCall("Viper.Threads.Thread.Start",
               {Value::temp(entryId), Value::temp(nullId)},
               Value::temp(threadId),
               {1, 2, 5});
    b.emitCall(
        "Viper.Threads.Thread.Join", {Value::temp(threadId)}, std::optional<Value>{}, {1, 2, 6});

    Instr loadFinal;
    loadFinal.result = b.reserveTempId();
    loadFinal.op = Opcode::Load;
    loadFinal.type = Type(Type::Kind::I64);
    loadFinal.operands.push_back(Value::temp(mainGPtrId));
    loadFinal.loc = {1, 2, 7};
    mainEntry.instructions.push_back(loadFinal);

    b.emitRet(Value::temp(*loadFinal.result), {1, 2, 8});

    il::vm::VM vm(m);
    const int64_t rc = vm.run();
    assert(rc == 42);
    return 0;
#endif
}
