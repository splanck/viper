//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/ThreadsRuntimeTests.cpp
// Purpose: Validate VM-side Viper.Threads integration (notably Thread.Start override).
// Key invariants: VM Thread.Start accepts IL function pointers and shares module globals.
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <cassert>

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

static void extern_worker_value(void **args, void *result) {
    (void)args;
    *static_cast<int64_t *>(result) = 777;
}

int main() {
    using namespace il::core;

    Module m;
    il::build::IRBuilder b(m);

    b.addGlobal("g", Type(Type::Kind::I64));
    b.addGlobal("g_thread_ext", Type(Type::Kind::I64));
    b.addGlobal("g_async_ext", Type(Type::Kind::I64));

    // Runtime externs (canonical names).
    b.addExtern("Viper.Threads.Thread.Start",
                Type(Type::Kind::Ptr),
                {Type(Type::Kind::Ptr), Type(Type::Kind::Ptr)});
    b.addExtern("Viper.Threads.Thread.Join", Type(Type::Kind::Void), {Type(Type::Kind::Ptr)});
    b.addExtern("Viper.Threads.Async.Run",
                Type(Type::Kind::Ptr),
                {Type(Type::Kind::Ptr), Type(Type::Kind::Ptr)});
    b.addExtern("Viper.Threads.Future.Get", Type(Type::Kind::Ptr), {Type(Type::Kind::Ptr)});
    b.addExtern("test.worker.value", Type(Type::Kind::I64), {});

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

    Instr threadExtPtr;
    threadExtPtr.result = b.reserveTempId();
    threadExtPtr.op = Opcode::GAddr;
    threadExtPtr.type = Type(Type::Kind::Ptr);
    threadExtPtr.operands.push_back(Value::global("g_thread_ext"));
    threadExtPtr.loc = {1, 1, 4};
    workerEntry.instructions.push_back(threadExtPtr);
    const unsigned threadExtPtrId = *threadExtPtr.result;

    Instr threadExtValue;
    threadExtValue.result = b.reserveTempId();
    threadExtValue.op = Opcode::Call;
    threadExtValue.type = Type(Type::Kind::I64);
    threadExtValue.callee = "test.worker.value";
    threadExtValue.loc = {1, 1, 4};
    workerEntry.instructions.push_back(threadExtValue);
    const unsigned threadExtValueId = *threadExtValue.result;

    Instr storeThreadExt;
    storeThreadExt.op = Opcode::Store;
    storeThreadExt.type = Type(Type::Kind::I64);
    storeThreadExt.operands.push_back(Value::temp(threadExtPtrId));
    storeThreadExt.operands.push_back(Value::temp(threadExtValueId));
    storeThreadExt.loc = {1, 1, 4};
    workerEntry.instructions.push_back(storeThreadExt);

    b.emitRet(std::optional<Value>{}, {1, 1, 5});

    // worker_async(ptr) -> ptr: g = g + 1; return null
    auto &workerAsync =
        b.startFunction("worker_async", Type(Type::Kind::Ptr), {{"env", Type(Type::Kind::Ptr)}});
    auto &workerAsyncEntry = b.createBlock(workerAsync, "entry", workerAsync.params);
    b.setInsertPoint(workerAsyncEntry);

    Instr asyncGAddr;
    asyncGAddr.result = b.reserveTempId();
    asyncGAddr.op = Opcode::GAddr;
    asyncGAddr.type = Type(Type::Kind::Ptr);
    asyncGAddr.operands.push_back(Value::global("g"));
    asyncGAddr.loc = {1, 1, 6};
    workerAsyncEntry.instructions.push_back(asyncGAddr);
    const unsigned asyncGPtrId = *asyncGAddr.result;

    Instr asyncLoadG;
    asyncLoadG.result = b.reserveTempId();
    asyncLoadG.op = Opcode::Load;
    asyncLoadG.type = Type(Type::Kind::I64);
    asyncLoadG.operands.push_back(Value::temp(asyncGPtrId));
    asyncLoadG.loc = {1, 1, 7};
    workerAsyncEntry.instructions.push_back(asyncLoadG);
    const unsigned asyncGValId = *asyncLoadG.result;

    Instr asyncAdd1;
    asyncAdd1.result = b.reserveTempId();
    asyncAdd1.op = Opcode::Add;
    asyncAdd1.type = Type(Type::Kind::I64);
    asyncAdd1.operands.push_back(Value::temp(asyncGValId));
    asyncAdd1.operands.push_back(Value::constInt(1));
    asyncAdd1.loc = {1, 1, 8};
    workerAsyncEntry.instructions.push_back(asyncAdd1);
    const unsigned asyncNextId = *asyncAdd1.result;

    Instr asyncStoreG;
    asyncStoreG.op = Opcode::Store;
    asyncStoreG.type = Type(Type::Kind::I64);
    asyncStoreG.operands.push_back(Value::temp(asyncGPtrId));
    asyncStoreG.operands.push_back(Value::temp(asyncNextId));
    asyncStoreG.loc = {1, 1, 9};
    workerAsyncEntry.instructions.push_back(asyncStoreG);

    Instr asyncExtPtr;
    asyncExtPtr.result = b.reserveTempId();
    asyncExtPtr.op = Opcode::GAddr;
    asyncExtPtr.type = Type(Type::Kind::Ptr);
    asyncExtPtr.operands.push_back(Value::global("g_async_ext"));
    asyncExtPtr.loc = {1, 1, 9};
    workerAsyncEntry.instructions.push_back(asyncExtPtr);
    const unsigned asyncExtPtrId = *asyncExtPtr.result;

    Instr asyncExtValue;
    asyncExtValue.result = b.reserveTempId();
    asyncExtValue.op = Opcode::Call;
    asyncExtValue.type = Type(Type::Kind::I64);
    asyncExtValue.callee = "test.worker.value";
    asyncExtValue.loc = {1, 1, 9};
    workerAsyncEntry.instructions.push_back(asyncExtValue);
    const unsigned asyncExtValueId = *asyncExtValue.result;

    Instr asyncStoreExt;
    asyncStoreExt.op = Opcode::Store;
    asyncStoreExt.type = Type(Type::Kind::I64);
    asyncStoreExt.operands.push_back(Value::temp(asyncExtPtrId));
    asyncStoreExt.operands.push_back(Value::temp(asyncExtValueId));
    asyncStoreExt.loc = {1, 1, 9};
    workerAsyncEntry.instructions.push_back(asyncStoreExt);

    Instr asyncNullRet;
    asyncNullRet.result = b.reserveTempId();
    asyncNullRet.op = Opcode::ConstNull;
    asyncNullRet.type = Type(Type::Kind::Ptr);
    asyncNullRet.loc = {1, 1, 10};
    workerAsyncEntry.instructions.push_back(asyncNullRet);
    b.emitRet(Value::temp(*asyncNullRet.result), {1, 1, 11});

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

    // Reset global and exercise Async.Run/Future.Get through the VM-aware extern bridge.
    Instr storeAsyncInit;
    storeAsyncInit.op = Opcode::Store;
    storeAsyncInit.type = Type(Type::Kind::I64);
    storeAsyncInit.operands.push_back(Value::temp(mainGPtrId));
    storeAsyncInit.operands.push_back(Value::constInt(41));
    storeAsyncInit.loc = {1, 2, 6};
    mainEntry.instructions.push_back(storeAsyncInit);

    Instr asyncEntryPtr;
    asyncEntryPtr.result = b.reserveTempId();
    asyncEntryPtr.op = Opcode::GAddr;
    asyncEntryPtr.type = Type(Type::Kind::Ptr);
    asyncEntryPtr.operands.push_back(Value::global("worker_async"));
    asyncEntryPtr.loc = {1, 2, 7};
    mainEntry.instructions.push_back(asyncEntryPtr);
    const unsigned asyncEntryId = *asyncEntryPtr.result;

    unsigned futureId = b.reserveTempId();
    b.emitCall("Viper.Threads.Async.Run",
               {Value::temp(asyncEntryId), Value::temp(nullId)},
               Value::temp(futureId),
               {1, 2, 8});

    unsigned awaitedId = b.reserveTempId();
    b.emitCall(
        "Viper.Threads.Future.Get", {Value::temp(futureId)}, Value::temp(awaitedId), {1, 2, 9});

    Instr loadFinal;
    loadFinal.result = b.reserveTempId();
    loadFinal.op = Opcode::Load;
    loadFinal.type = Type(Type::Kind::I64);
    loadFinal.operands.push_back(Value::temp(mainGPtrId));
    loadFinal.loc = {1, 2, 10};
    mainEntry.instructions.push_back(loadFinal);

    Instr mainThreadExtPtr;
    mainThreadExtPtr.result = b.reserveTempId();
    mainThreadExtPtr.op = Opcode::GAddr;
    mainThreadExtPtr.type = Type(Type::Kind::Ptr);
    mainThreadExtPtr.operands.push_back(Value::global("g_thread_ext"));
    mainThreadExtPtr.loc = {1, 2, 10};
    mainEntry.instructions.push_back(mainThreadExtPtr);

    Instr loadThreadExt;
    loadThreadExt.result = b.reserveTempId();
    loadThreadExt.op = Opcode::Load;
    loadThreadExt.type = Type(Type::Kind::I64);
    loadThreadExt.operands.push_back(Value::temp(*mainThreadExtPtr.result));
    loadThreadExt.loc = {1, 2, 10};
    mainEntry.instructions.push_back(loadThreadExt);

    Instr mainAsyncExtPtr;
    mainAsyncExtPtr.result = b.reserveTempId();
    mainAsyncExtPtr.op = Opcode::GAddr;
    mainAsyncExtPtr.type = Type(Type::Kind::Ptr);
    mainAsyncExtPtr.operands.push_back(Value::global("g_async_ext"));
    mainAsyncExtPtr.loc = {1, 2, 10};
    mainEntry.instructions.push_back(mainAsyncExtPtr);

    Instr loadAsyncExt;
    loadAsyncExt.result = b.reserveTempId();
    loadAsyncExt.op = Opcode::Load;
    loadAsyncExt.type = Type(Type::Kind::I64);
    loadAsyncExt.operands.push_back(Value::temp(*mainAsyncExtPtr.result));
    loadAsyncExt.loc = {1, 2, 10};
    mainEntry.instructions.push_back(loadAsyncExt);

    Instr sumThread;
    sumThread.result = b.reserveTempId();
    sumThread.op = Opcode::Add;
    sumThread.type = Type(Type::Kind::I64);
    sumThread.operands.push_back(Value::temp(*loadFinal.result));
    sumThread.operands.push_back(Value::temp(*loadThreadExt.result));
    sumThread.loc = {1, 2, 10};
    mainEntry.instructions.push_back(sumThread);

    Instr sumAll;
    sumAll.result = b.reserveTempId();
    sumAll.op = Opcode::Add;
    sumAll.type = Type(Type::Kind::I64);
    sumAll.operands.push_back(Value::temp(*sumThread.result));
    sumAll.operands.push_back(Value::temp(*loadAsyncExt.result));
    sumAll.loc = {1, 2, 10};
    mainEntry.instructions.push_back(sumAll);

    b.emitRet(Value::temp(*sumAll.result), {1, 2, 11});

    il::vm::VM vm(m);
    auto reg = il::vm::createExternRegistry();
    il::vm::ExternDesc desc;
    desc.name = "test.worker.value";
    desc.signature = make_signature("test.worker.value", {}, {SigParam::I64});
    desc.fn = reinterpret_cast<void *>(&extern_worker_value);
    il::vm::registerExternIn(*reg, desc);
    vm.setExternRegistry(reg.get());
    reg.reset();

    const int64_t rc = vm.run();
    assert(rc == (42 + 777 + 777));
    return 0;
}
