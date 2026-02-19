//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/P1OptimizationTests.cpp
// Purpose: Verify correctness of Priority-1 VM hot-path optimisations:
//   P1-3.3  flat Slot+paramsSet vectors replacing optional<Slot> params
//   P1-3.4  VM-level SwitchCache persistence across function calls
//   P1-3.5  raw fn-pointer pollCallback trampoline
//   P1-3.1/2 FunctionExecCache pre-resolved operand arrays
// Key invariants:
//   - All tests verify observable output, not just internal structure.
//   - Structural checks are added only where the optimisation could silently
//     produce a wrong answer if the invariant is violated.
// Ownership/Lifetime: builds synthetic modules per test; no shared state.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "unit/VMTestHook.hpp"
#include "vm/VM.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace il::core;
using namespace il::vm;

// ============================================================================
// Common helpers
// ============================================================================

static constexpr il::support::SourceLoc kLoc{1, 1, 0};

/// @brief Fail a test with a message and return 1.
static int fail(const char *testName, const char *msg, long long got = -1)
{
    if (got != -1)
        std::fprintf(stderr, "[FAIL] %s: %s (got %lld)\n", testName, msg, got);
    else
        std::fprintf(stderr, "[FAIL] %s: %s\n", testName, msg);
    return 1;
}

// ============================================================================
// Test 1: P1-3.3 — flat Slot + paramsSet block-param correctness
// ============================================================================
/// @details Runs a counting loop that passes an I64 counter through a block
/// param.  The flat params/paramsSet vectors must correctly stage and transfer
/// the updated value on every back-edge.
static int test_flat_params_correctness()
{
    static const char *kName = "test_flat_params_correctness";
    constexpr int64_t kLimit = 7;

    Module module;
    il::build::IRBuilder builder(module);

    // main() -> i64 — no function params
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});

    // Create all blocks first (fn.blocks reallocation would invalidate refs)
    builder.createBlock(fn, "entry", {});
    builder.createBlock(fn, "loop", {Param{"counter", Type(Type::Kind::I64), 0}});
    builder.createBlock(fn, "body", {});
    builder.createBlock(fn, "exit", {Param{"result", Type(Type::Kind::I64), 0}});

    auto *entry = &fn.blocks[0];
    auto *loop = &fn.blocks[1];
    auto *body = &fn.blocks[2];
    auto *exitBlk = &fn.blocks[3];

    // entry: br loop(counter=0)
    {
        builder.setInsertPoint(*entry);
        Instr jmp;
        jmp.op = Opcode::Br;
        jmp.type = Type(Type::Kind::Void);
        jmp.loc = kLoc;
        jmp.labels.push_back("loop");
        jmp.brArgs.push_back({Value::constInt(0)});
        entry->instructions.push_back(jmp);
        entry->terminated = true;
    }

    // loop: %cmp = slt counter, kLimit; cbr %cmp, body, exit(counter)
    {
        builder.setInsertPoint(*loop);
        Instr slt;
        slt.result = builder.reserveTempId();
        slt.op = Opcode::SCmpLT;
        slt.type = Type(Type::Kind::I1);
        slt.loc = kLoc;
        slt.operands.push_back(builder.blockParam(*loop, 0)); // counter
        slt.operands.push_back(Value::constInt(kLimit));
        loop->instructions.push_back(slt);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.loc = kLoc;
        cbr.operands.push_back(Value::temp(*slt.result));
        cbr.labels.push_back("body");
        cbr.labels.push_back("exit");
        cbr.brArgs.push_back({});                             // body: no args
        cbr.brArgs.push_back({builder.blockParam(*loop, 0)}); // exit(result=counter)
        loop->instructions.push_back(cbr);
        loop->terminated = true;
    }

    // body: %next = add counter, 1; br loop(%next)
    {
        builder.setInsertPoint(*body);
        Instr add;
        add.result = builder.reserveTempId();
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.loc = kLoc;
        add.operands.push_back(builder.blockParam(*loop, 0)); // counter
        add.operands.push_back(Value::constInt(1));
        body->instructions.push_back(add);

        Instr jmp;
        jmp.op = Opcode::Br;
        jmp.type = Type(Type::Kind::Void);
        jmp.loc = kLoc;
        jmp.labels.push_back("loop");
        jmp.brArgs.push_back({Value::temp(*add.result)});
        body->instructions.push_back(jmp);
        body->terminated = true;
    }

    // exit: ret result
    {
        builder.setInsertPoint(*exitBlk);
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = kLoc;
        ret.operands.push_back(builder.blockParam(*exitBlk, 0)); // result
        exitBlk->instructions.push_back(ret);
        exitBlk->terminated = true;
    }

    VM vm(module);
    const Slot result = VMTestHook::run(vm, fn, {});
    if (result.i64 != kLimit)
        return fail(kName, "wrong loop result", result.i64);
    return 0;
}

// ============================================================================
// Test 2: P1-3.4 — VM-level SwitchCache persists across calls
// ============================================================================
/// @details Calls a switch function twice on the same VM.  After the first
/// call the switch cache must be non-empty; after the second call the size
/// must be identical — confirming that entries were reused rather than rebuilt.
static int test_switch_cache_persistence()
{
    static const char *kName = "test_switch_cache_persistence";

    Module module;
    {
        il::build::IRBuilder builder(module);
        auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
        builder.createBlock(fn, "entry", {});
        builder.createBlock(fn, "hit", {});
        builder.createBlock(fn, "miss", {});

        auto &entry = fn.blocks[0];
        auto &hit = fn.blocks[1];
        auto &miss = fn.blocks[2];

        // entry: SwitchI32(2, default=miss, [1 → hit])
        {
            Instr sw;
            sw.op = Opcode::SwitchI32;
            sw.type = Type(Type::Kind::Void);
            sw.loc = kLoc;
            sw.operands.push_back(Value::constInt(2)); // scrutinee
            sw.labels.push_back("miss");               // default
            sw.brArgs.emplace_back();
            sw.operands.push_back(Value::constInt(1)); // case value 1
            sw.labels.push_back("hit");
            sw.brArgs.emplace_back();
            entry.instructions.push_back(sw);
            entry.terminated = true;
        }
        // hit: ret 42
        {
            Instr ret;
            ret.op = Opcode::Ret;
            ret.type = Type(Type::Kind::Void);
            ret.loc = kLoc;
            ret.operands.push_back(Value::constInt(42));
            hit.instructions.push_back(ret);
            hit.terminated = true;
        }
        // miss: ret 0
        {
            Instr ret;
            ret.op = Opcode::Ret;
            ret.type = Type(Type::Kind::Void);
            ret.loc = kLoc;
            ret.operands.push_back(Value::constInt(0));
            miss.instructions.push_back(ret);
            miss.terminated = true;
        }
    }

    VM vm(module);
    const il::core::Function &fn = module.functions.front();

    // First call: scrutinee=2 doesn't match case 1 → miss → 0
    const Slot r1 = VMTestHook::run(vm, fn, {});
    if (r1.i64 != 0)
        return fail(kName, "first call: expected 0", r1.i64);

    const size_t cacheAfterFirst = VMTestHook::switchCacheSize(vm);
    if (cacheAfterFirst == 0)
        return fail(kName, "switch cache empty after first call");

    // Second call: must produce the same result and must NOT increase cache size
    const Slot r2 = VMTestHook::run(vm, fn, {});
    if (r2.i64 != 0)
        return fail(kName, "second call: expected 0", r2.i64);

    const size_t cacheAfterSecond = VMTestHook::switchCacheSize(vm);
    if (cacheAfterSecond != cacheAfterFirst)
    {
        std::fprintf(stderr,
                     "[FAIL] %s: cache size changed (%zu → %zu); "
                     "entries were rebuilt instead of reused\n",
                     kName,
                     cacheAfterFirst,
                     cacheAfterSecond);
        return 1;
    }
    return 0;
}

// ============================================================================
// Test 3: P1-3.5 — raw fn-pointer poll trampoline fires correctly
// ============================================================================
/// @details Installs a counting poll callback via VMTestHook::setPoll, then
/// runs a function with several instructions.  Verifies that:
///   a) ExecState::config.pollCallback is non-null (trampoline installed)
///   b) The callback fires at least once during execution
static int test_poll_callback_trampoline()
{
    static const char *kName = "test_poll_callback_trampoline";

    Module module;
    {
        il::build::IRBuilder builder(module);
        auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
        auto &bb = builder.createBlock(fn, "entry", {});
        builder.setInsertPoint(bb);

        // Emit 6 add instructions to give the poller enough opportunities.
        // Each one adds a constant to the previous result.
        unsigned prev = 0;
        bool first = true;
        for (int i = 1; i <= 6; ++i)
        {
            Instr add;
            add.result = builder.reserveTempId();
            add.op = Opcode::Add;
            add.type = Type(Type::Kind::I64);
            add.loc = kLoc;
            if (first)
            {
                add.operands.push_back(Value::constInt(0));
                first = false;
            }
            else
            {
                add.operands.push_back(Value::temp(prev));
            }
            add.operands.push_back(Value::constInt(i));
            bb.instructions.push_back(add);
            prev = *add.result;
        }

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = kLoc;
        ret.operands.push_back(Value::temp(prev));
        bb.instructions.push_back(ret);
        bb.terminated = true;
    }

    std::atomic<int> callCount{0};

    VM vm(module);
    // Poll every 2 instructions; function has 6 adds + 1 ret = 7 instructions
    VMTestHook::setPoll(vm,
                        2,
                        [&callCount](VM &) -> bool
                        {
                            ++callCount;
                            return true; // continue execution
                        });

    // Verify that the trampoline fn ptr is installed in a fresh ExecState
    {
        auto &fn = module.functions.front();
        auto state = VMTestHook::prepare(vm, fn);
        if (!VMTestHook::hasPollFnPtr(state))
            return fail(kName, "pollCallback fn ptr is null — trampoline not installed");
    }

    // Run the function through the normal dispatch path
    const Slot result = VMTestHook::run(vm, module.functions.front(), {});
    // 0+1+2+3+4+5+6 = 21
    if (result.i64 != 21)
        return fail(kName, "wrong computation result", result.i64);

    if (callCount.load() == 0)
        return fail(kName, "poll callback never fired");

    return 0;
}

// ============================================================================
// Test 4: P1-3.1/3.2 — FunctionExecCache structure and Reg operands
// ============================================================================
/// @details Builds an add-two-params function.  After prepareExecution the
/// block cache must be non-null and the add instruction's operands must be
/// resolved as Kind::Reg.  Running the function must return the correct sum.
static int test_exec_cache_reg_operands()
{
    static const char *kName = "test_exec_cache_reg_operands";

    Module module;
    // Function args are passed as entry-block parameters in this VM (SSA convention).
    {
        il::build::IRBuilder builder(module);
        auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
        auto &bb = builder.createBlock(
            fn,
            "entry",
            {Param{"a", Type(Type::Kind::I64), 0}, Param{"b", Type(Type::Kind::I64), 0}});
        builder.setInsertPoint(bb);

        Instr add;
        add.result = builder.reserveTempId();
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.loc = kLoc;
        add.operands.push_back(builder.blockParam(bb, 0)); // a
        add.operands.push_back(builder.blockParam(bb, 1)); // b
        bb.instructions.push_back(add);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = kLoc;
        ret.operands.push_back(Value::temp(*add.result));
        bb.instructions.push_back(ret);
        bb.terminated = true;
    }

    VM vm(module);
    auto &fn = module.functions.front();
    const auto &entryBb = fn.blocks.front();

    // Capture the IRBuilder-assigned param IDs for structural checks
    const unsigned expectedAId = entryBb.params[0].id;
    const unsigned expectedBId = entryBb.params[1].id;

    // Seed arguments: a=30, b=12
    Slot sa{}, sb{};
    sa.i64 = 30;
    sb.i64 = 12;
    const std::vector<Slot> args{sa, sb};

    auto state = VMTestHook::prepare(vm, fn, args);

    // Check that blockCache was populated
    const BlockExecCache *bc = VMTestHook::blockCache(state);
    if (!bc)
        return fail(kName, "blockCache is null after prepareExecution");

    // entry block has 2 instructions: add + ret
    if (bc->instrOpOffset.size() != 2)
    {
        std::fprintf(stderr,
                     "[FAIL] %s: expected 2 instrOpOffset entries, got %zu\n",
                     kName,
                     bc->instrOpOffset.size());
        return 1;
    }

    // add instruction at offset 0: two Reg operands whose regId matches param IDs
    const uint32_t addOff = bc->instrOpOffset[0];
    if (bc->resolvedOps[addOff].kind != ResolvedOp::Kind::Reg)
        return fail(kName, "add operand[0] expected Kind::Reg");
    if (bc->resolvedOps[addOff].regId != expectedAId)
        return fail(kName, "add operand[0] regId mismatch", bc->resolvedOps[addOff].regId);
    if (bc->resolvedOps[addOff + 1].kind != ResolvedOp::Kind::Reg)
        return fail(kName, "add operand[1] expected Kind::Reg");
    if (bc->resolvedOps[addOff + 1].regId != expectedBId)
        return fail(kName, "add operand[1] regId mismatch", bc->resolvedOps[addOff + 1].regId);

    // Run and verify
    const Slot result = VMTestHook::run(vm, fn, args);
    if (result.i64 != 42)
        return fail(kName, "expected 42", result.i64);
    return 0;
}

// ============================================================================
// Test 5: P1-3.1/3.2 — FunctionExecCache ImmI64 operands
// ============================================================================
/// @details Verifies that ConstInt operands become Kind::ImmI64 with the
/// correct numeric payload.
static int test_exec_cache_imm_operands()
{
    static const char *kName = "test_exec_cache_imm_operands";

    Module module;
    {
        il::build::IRBuilder builder(module);
        auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
        auto &bb = builder.createBlock(fn, "entry", {});
        builder.setInsertPoint(bb);

        Instr add;
        add.result = builder.reserveTempId();
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.loc = kLoc;
        add.operands.push_back(Value::constInt(100));
        add.operands.push_back(Value::constInt(23));
        bb.instructions.push_back(add);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.loc = kLoc;
        ret.operands.push_back(Value::temp(*add.result));
        bb.instructions.push_back(ret);
        bb.terminated = true;
    }

    VM vm(module);
    auto &fn = module.functions.front();

    auto state = VMTestHook::prepare(vm, fn);
    const BlockExecCache *bc = VMTestHook::blockCache(state);
    if (!bc)
        return fail(kName, "blockCache is null");

    const uint32_t off = bc->instrOpOffset[0];

    if (bc->resolvedOps[off].kind != ResolvedOp::Kind::ImmI64)
        return fail(kName, "add operand[0] expected Kind::ImmI64");
    if (bc->resolvedOps[off].numVal != 100)
        return fail(kName, "add operand[0] expected numVal 100", bc->resolvedOps[off].numVal);

    if (bc->resolvedOps[off + 1].kind != ResolvedOp::Kind::ImmI64)
        return fail(kName, "add operand[1] expected Kind::ImmI64");
    if (bc->resolvedOps[off + 1].numVal != 23)
        return fail(kName, "add operand[1] expected numVal 23", bc->resolvedOps[off + 1].numVal);

    const Slot result = VMTestHook::run(vm, fn, {});
    if (result.i64 != 123)
        return fail(kName, "expected 123", result.i64);
    return 0;
}

// ============================================================================
// main
// ============================================================================
int main()
{
    int failures = 0;
    failures += test_flat_params_correctness();
    failures += test_switch_cache_persistence();
    failures += test_poll_callback_trampoline();
    failures += test_exec_cache_reg_operands();
    failures += test_exec_cache_imm_operands();
    return failures;
}
