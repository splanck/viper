//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/InterruptTests.cpp
// Purpose: Verify the VM interrupt mechanism (HIGH-3, MED-1 from cross-platform
//          compatibility report).
//
//   HIGH-3: VM.requestInterrupt() sets an atomic flag that causes the dispatch
//           loop to raise a TrapKind::Interrupt before the next function call.
//           This is the same flag that the SIGINT / SetConsoleCtrlHandler
//           handlers set on Unix / Windows respectively.
//
//   The test uses a short-running infinite loop program and fires the interrupt
//   flag programmatically (via the poll callback).  Because an unhandled
//   TrapKind::Interrupt causes rt_abort (terminating the process), the
//   testInterruptFires case runs the VM in a forked child process and captures
//   stderr — the same technique used by VmFixture::captureTrap.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "unit/VMTestHook.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cstdio>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace il::core;
using namespace il::vm;

static constexpr il::support::SourceLoc kLoc{1, 1, 0};

/// @brief Build a module containing a tight loop: while(true) {}
/// @details Creates entry → loop → loop (back-edge).  The dispatch driver will
///          spin indefinitely unless interrupted via poll or an external signal.
static void buildInfiniteLoopModule(Module &mod)
{
    il::build::IRBuilder b(mod);
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});

    // Create all blocks first — push_back invalidates existing references.
    b.createBlock(fn, "entry", {});
    b.createBlock(fn, "loop", {});

    // entry → loop
    b.setInsertPoint(fn.blocks[0]);
    b.br(fn.blocks[1]);

    // loop → loop (back edge = infinite loop)
    b.setInsertPoint(fn.blocks[1]);
    b.br(fn.blocks[1]);
}

/// @brief Build a module that immediately returns 42.
static void buildReturnModule(Module &mod)
{
    il::build::IRBuilder b(mod);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.createBlock(fn, "entry", {});
    b.setInsertPoint(entry);
    b.emitRet(Value::constInt(42), kLoc);
}

// =============================================================================
// Test: requestInterrupt / clearInterrupt API
// =============================================================================

static int testInterruptApi()
{
    // clearInterrupt should be idempotent when no interrupt is pending.
    VM::clearInterrupt();

    // requestInterrupt sets the flag.
    VM::requestInterrupt();

    // clearInterrupt resets it.
    VM::clearInterrupt();

    std::fprintf(stdout, "[PASS] interrupt API (requestInterrupt/clearInterrupt)\n");
    return 0;
}

// =============================================================================
// Test: Interrupt fires and produces a trapped VM state
// =============================================================================

static int testInterruptFires()
{
    // An unhandled TrapKind::Interrupt calls rt_abort which terminates the
    // process.  Run the VM in a forked child so the parent can capture the
    // trap diagnostic from stderr and verify the trap fired correctly.
#if defined(_WIN32)
    std::fprintf(stdout,
                 "[SKIP] testInterruptFires: subprocess capture not available on Windows\n");
    return 0;
#else
    Module mod;
    buildInfiniteLoopModule(mod);

    // Flush parent stdio before forking so the child inherits an empty buffer.
    std::fflush(stdout);
    std::fflush(stderr);

    // Pipe to capture the child's stderr output.
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0)
    {
        std::fprintf(stderr, "[FAIL] testInterruptFires: pipe() failed\n");
        return 1;
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        std::fprintf(stderr, "[FAIL] testInterruptFires: fork() failed\n");
        return 1;
    }

    if (pid == 0)
    {
        // Child: redirect stderr into the write end of the pipe, then run the VM.
        ::close(fds[0]);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);

        VM::clearInterrupt();
        VM vm(mod);

        // After 500 instructions, request an interrupt and stop the driver by
        // returning false.  runFunctionLoop checks s_interruptRequested after
        // dispatchDriver->run() returns and raises TrapKind::Interrupt.
        int callCount = 0;
        VMTestHook::setPoll(vm,
                            500,
                            [&callCount](VM &) -> bool
                            {
                                if (++callCount == 1)
                                    VM::requestInterrupt();
                                return false; // Stop the driver so the post-dispatch check fires.
                            });

        vm.run();
        ::_exit(0); // Unreachable: rt_abort terminates the child first.
    }

    // Parent: read stderr captured from the child.
    ::close(fds[1]);
    std::string buffer;
    std::array<char, 512> tmp{};
    while (true)
    {
        const ssize_t n = ::read(fds[0], tmp.data(), tmp.size());
        if (n <= 0)
            break;
        buffer.append(tmp.data(), static_cast<std::size_t>(n));
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    // The child must have terminated with a non-zero exit code (rt_abort).
    const bool exitedNonZero = WIFEXITED(status) && WEXITSTATUS(status) != 0;
    const bool signaled = WIFSIGNALED(status);
    if (!exitedNonZero && !signaled)
    {
        std::fprintf(stderr, "[FAIL] testInterruptFires: child exited cleanly (expected trap)\n");
        return 1;
    }

    // stderr must mention 'Interrupt'.
    if (buffer.find("Interrupt") == std::string::npos &&
        buffer.find("interrupt") == std::string::npos)
    {
        std::fprintf(stderr,
                     "[FAIL] testInterruptFires: trap output does not mention 'interrupt': %s\n",
                     buffer.c_str());
        return 1;
    }

    // Strip trailing newline for the pass message.
    while (!buffer.empty() && (buffer.back() == '\n' || buffer.back() == '\r'))
        buffer.pop_back();

    std::fprintf(stdout, "[PASS] interrupt fires cleanly (trap: %s)\n", buffer.c_str());
    return 0;
#endif
}

// =============================================================================
// Test: Normal program is unaffected by clearInterrupt
// =============================================================================

static int testNormalProgramAfterClear()
{
    VM::clearInterrupt();

    Module mod;
    buildReturnModule(mod);
    VM vm(mod);
    const int64_t result = vm.run();

    if (result != 42)
    {
        std::fprintf(stderr,
                     "[FAIL] testNormalProgramAfterClear: expected 42, got %lld\n",
                     (long long)result);
        return 1;
    }

    std::fprintf(stdout, "[PASS] normal program unaffected (got 42)\n");
    return 0;
}

int main()
{
    int failures = 0;
    failures += testInterruptApi();
    failures += testInterruptFires();
    failures += testNormalProgramAfterClear();

    if (failures == 0)
        std::fprintf(stdout, "[PASS] all interrupt tests passed\n");
    else
        std::fprintf(stderr, "[FAIL] %d interrupt test(s) failed\n", failures);

    return failures > 0 ? 1 : 0;
}
