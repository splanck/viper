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

#include "il/build/IRBuilder.hpp"
#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include "viper/vm/VM.hpp"
#include <cassert>
#include <cstdlib>
#include <string>

/// Build a simple module that allocates 'bytes' on the stack and returns 0.
static il::core::Module buildAllocaModule(int64_t bytes)
{
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
static void testLargeStackAllocation()
{
    // Allocate 1MB - exceeds default 64KB but should work with 2MB stack.
    constexpr int64_t allocSize = 1024 * 1024;         // 1MB
    constexpr std::size_t stackSize = 2 * 1024 * 1024; // 2MB

    auto m = buildAllocaModule(allocSize);

    il::vm::RunConfig config;
    config.stackBytes = stackSize;

    int64_t result = il::vm::runModule(m, config);
    assert(result == 0 && "Large allocation with large stack should succeed");
}

/// Test 2: Small stack size triggers overflow on allocations that fit in default.
static void testSmallStackOverflow()
{
    // Allocate 32KB - would fit in default 64KB but not in 16KB stack.
    constexpr int64_t allocSize = 32 * 1024;     // 32KB
    constexpr std::size_t stackSize = 16 * 1024; // 16KB

    auto m = buildAllocaModule(allocSize);

    // Fork to capture trap output
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], STDERR_FILENO);

        il::vm::RunConfig config;
        config.stackBytes = stackSize;
        (void)il::vm::runModule(m, config);
        _exit(0);
    }

    close(fds[1]);
    char buf[512];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';

    int status = 0;
    waitpid(pid, &status, 0);

    std::string out(buf);
    bool hasOverflow = out.find("stack overflow in alloca") != std::string::npos;
    assert(hasOverflow && "Small stack should trap on large allocation");
}

/// Test 3: Default stack size (0 in config) behaves like 64KB.
static void testDefaultStackSize()
{
    // Allocate 32KB - should succeed with default stack.
    constexpr int64_t allocSize = 32 * 1024;

    auto m = buildAllocaModule(allocSize);

    il::vm::RunConfig config;
    config.stackBytes = 0; // Should use default 64KB

    int64_t result = il::vm::runModule(m, config);
    assert(result == 0 && "Default stack should handle 32KB allocation");
}

/// Test 4: Very small stack (256 bytes) traps on any significant allocation.
static void testVerySmallStack()
{
    constexpr int64_t allocSize = 512;     // 512 bytes
    constexpr std::size_t stackSize = 256; // 256 bytes

    auto m = buildAllocaModule(allocSize);

    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], STDERR_FILENO);

        il::vm::RunConfig config;
        config.stackBytes = stackSize;
        (void)il::vm::runModule(m, config);
        _exit(0);
    }

    close(fds[1]);
    char buf[512];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';

    int status = 0;
    waitpid(pid, &status, 0);

    std::string out(buf);
    bool hasOverflow = out.find("stack overflow in alloca") != std::string::npos;
    assert(hasOverflow && "Very small stack should trap on 512-byte allocation");
}

int main()
{
    SKIP_TEST_NO_FORK();
    testLargeStackAllocation();
    testSmallStackOverflow();
    testDefaultStackSize();
    testVerySmallStack();
    return 0;
}
