//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_stack_size_config.cpp
// Purpose: Verify that VM stack size is configurable via RunConfig.stackBytes.
// Key invariants:
//   - Large stackBytes allows allocations that exceed the default 64KB.
//   - Small stackBytes triggers overflow on allocations that fit in default.
// Ownership/Lifetime: Test constructs IL module and executes VM with configs.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/build/IRBuilder.hpp"
#include "viper/vm/VM.hpp"
#include <cassert>
#include <cstdlib>
#include <string>

/// Build a simple module that allocates 'bytes' on the stack and returns 0.
static il::core::Module buildAllocaModule(int64_t bytes) {
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");

    // alloca ptr %0 = bytes
    il::core::Instr alloca;
    alloca.op = il::core::Opcode::Alloca;
    alloca.type = il::core::Type(il::core::Type::Kind::Ptr);
    alloca.operands.push_back(il::core::Value::constInt(bytes));
    alloca.result = 0;
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    // ret i64 0
    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::constInt(0));
    ret.loc = {1, 1, 1};
    bb.instructions.push_back(ret);
    bb.terminated = true;

    return m;
}

/// Test 1: Large stack size allows allocations beyond the default 64KB.
static void testLargeStackAllocation() {
    // Allocate 1MB - exceeds default 64KB but should work with 2MB stack.
    constexpr int64_t allocSize = 1024 * 1024;         // 1MB
    constexpr std::size_t stackSize = 2 * 1024 * 1024; // 2MB

    auto m = buildAllocaModule(allocSize);

    il::vm::RunConfig config;
    config.stackBytes = stackSize;

    int64_t result = il::vm::runModule(m, config);
    assert(result == 0 && "Large allocation with large stack should succeed");
}

/// Child function for test 2: 32KB alloc on 16KB stack.
static void runSmallStackOverflow() {
    auto m = buildAllocaModule(32 * 1024);
    il::vm::RunConfig config;
    config.stackBytes = 16 * 1024;
    (void)il::vm::runModule(m, config);
}

/// Test 2: Small stack size triggers overflow on allocations that fit in default.
static void testSmallStackOverflow() {
    auto result = viper::tests::runIsolated(runSmallStackOverflow);
    assert(result.trapped());
    bool hasOverflow = result.stderrText.find("stack overflow in alloca") != std::string::npos;
    assert(hasOverflow && "Small stack should trap on large allocation");
}

/// Test 3: Default stack size (0 in config) behaves like 64KB.
static void testDefaultStackSize() {
    // Allocate 32KB - should succeed with default stack.
    constexpr int64_t allocSize = 32 * 1024;

    auto m = buildAllocaModule(allocSize);

    il::vm::RunConfig config;
    config.stackBytes = 0; // Should use default 64KB

    int64_t result = il::vm::runModule(m, config);
    assert(result == 0 && "Default stack should handle 32KB allocation");
}

/// Child function for test 4: 512-byte alloc on 256-byte stack.
static void runVerySmallStack() {
    auto m = buildAllocaModule(512);
    il::vm::RunConfig config;
    config.stackBytes = 256;
    (void)il::vm::runModule(m, config);
}

/// Test 4: Very small stack (256 bytes) traps on any significant allocation.
static void testVerySmallStack() {
    auto result = viper::tests::runIsolated(runVerySmallStack);
    assert(result.trapped());
    bool hasOverflow = result.stderrText.find("stack overflow in alloca") != std::string::npos;
    assert(hasOverflow && "Very small stack should trap on 512-byte allocation");
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(runSmallStackOverflow);
    viper::tests::registerChildFunction(runVerySmallStack);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    testLargeStackAllocation();
    testSmallStackOverflow();
    testDefaultStackSize();
    testVerySmallStack();
    return 0;
}
