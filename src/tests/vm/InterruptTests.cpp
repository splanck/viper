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
//   testInterruptFires case runs the VM in an isolated child process and
//   captures stderr using the ProcessIsolation API.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "unit/VMTestHook.hpp"
#include "vm/VM.hpp"

#include "common/ProcessIsolation.hpp"
#include <array>
#include <cstdio>
#include <string>

using namespace il::core;
using namespace il::vm;

static constexpr il::support::SourceLoc kLoc{1, 1, 0};

/// @brief Build a module containing a tight loop: while(true) {}
/// @details Creates entry -> loop -> loop (back-edge).  The dispatch driver will
///          spin indefinitely unless interrupted via poll or an external signal.
static void buildInfiniteLoopModule(Module &mod) {
    il::build::IRBuilder b(mod);
    auto &fn = b.startFunction("main", Type(Type::Kind::Void), {});

    // Create all blocks first -- push_back invalidates existing references.
    b.createBlock(fn, "entry", {});
    b.createBlock(fn, "loop", {});

    // entry -> loop
    b.setInsertPoint(fn.blocks[0]);
    b.br(fn.blocks[1]);

    // loop -> loop (back edge = infinite loop)
    b.setInsertPoint(fn.blocks[1]);
    b.br(fn.blocks[1]);
}

/// @brief Build a module that immediately returns 42.
static void buildReturnModule(Module &mod) {
    il::build::IRBuilder b(mod);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.createBlock(fn, "entry", {});
    b.setInsertPoint(entry);
    b.emitRet(Value::constInt(42), kLoc);
}

// =============================================================================
// Test: requestInterrupt / clearInterrupt API
// =============================================================================

static int testInterruptApi() {
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

static void runInterruptChild() {
    Module mod;
    buildInfiniteLoopModule(mod);

    VM::clearInterrupt();
    VM vm(mod);

    // After 500 instructions, request an interrupt and stop the driver by
    // returning false.  runFunctionLoop checks s_interruptRequested after
    // dispatchDriver->run() returns and raises TrapKind::Interrupt.
    int callCount = 0;
    VMTestHook::setPoll(vm, 500, [&callCount](VM &) -> bool {
        if (++callCount == 1)
            VM::requestInterrupt();
        return false; // Stop the driver so the post-dispatch check fires.
    });

    vm.run();
    // Unreachable: rt_abort terminates the child first.
}

static int testInterruptFires() {
    auto result = viper::tests::runIsolated(runInterruptChild);

    // The child must have terminated with a non-zero exit code (rt_abort).
    if (!result.trapped()) {
        std::fprintf(stderr, "[FAIL] testInterruptFires: child exited cleanly (expected trap)\n");
        return 1;
    }

    // stderr must mention 'Interrupt'.
    if (result.stderrText.find("Interrupt") == std::string::npos &&
        result.stderrText.find("interrupt") == std::string::npos) {
        std::fprintf(stderr,
                     "[FAIL] testInterruptFires: trap output does not mention 'interrupt': %s\n",
                     result.stderrText.c_str());
        return 1;
    }

    // Strip trailing newline for the pass message.
    std::string buffer = result.stderrText;
    while (!buffer.empty() && (buffer.back() == '\n' || buffer.back() == '\r'))
        buffer.pop_back();

    std::fprintf(stdout, "[PASS] interrupt fires cleanly (trap: %s)\n", buffer.c_str());
    return 0;
}

// =============================================================================
// Test: Normal program is unaffected by clearInterrupt
// =============================================================================

static int testNormalProgramAfterClear() {
    VM::clearInterrupt();

    Module mod;
    buildReturnModule(mod);
    VM vm(mod);
    const int64_t result = vm.run();

    if (result != 42) {
        std::fprintf(stderr,
                     "[FAIL] testNormalProgramAfterClear: expected 42, got %lld\n",
                     (long long)result);
        return 1;
    }

    std::fprintf(stdout, "[PASS] normal program unaffected (got 42)\n");
    return 0;
}

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(runInterruptChild);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

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
