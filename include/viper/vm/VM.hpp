//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/VM.hpp
// Purpose: Declare a lightweight facade for running IL modules through the VM
//          without exposing interpreter internals.
// Key invariants: Public API owns its backing VM implementation and forwards all
//                 operations while preserving semantics of the existing VM class.
// Ownership/Lifetime: Runner manages the interpreter lifetime; callers retain
//                     ownership of modules and optional debug scripts passed in
//                     via configuration.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//
//
// UNKNOWN/UNIMPLEMENTED OPCODE HANDLING
// ======================================
// Under normal circumstances, all opcodes defined in the IL specification have
// handlers in the VM dispatch table. This is enforced by dispatch coverage tests
// (test_vm_dispatch_coverage) which verify at compile-time and run-time that
// every opcode has a corresponding handler.
//
// If an unknown or unimplemented opcode is somehow executed (e.g., due to a
// mismatched code generator and VM version, or a corrupted IL module), the VM
// treats this as a FATAL ERROR:
//   1. A trap of kind RuntimeError is raised with a message including the
//      opcode mnemonic and execution context.
//   2. The trap propagates to the RuntimeBridge, which terminates the process
//      via rt_abort() since this condition is not recoverable.
//
// This behavior is intentional: an unknown opcode indicates a programmer error
// or version mismatch, not a runtime condition that can be caught or recovered.
// Embedders should NOT rely on trapping or continuing after an unknown opcode;
// instead, ensure that IL modules are generated with a code generator compatible
// with the VM version in use.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "viper/vm/debug/Debug.hpp"

#include "il/core/Opcode.hpp"
#include "support/source_location.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace il::core
{
struct Module;
} // namespace il::core

namespace il::vm
{
class VM; // forward declaration for callbacks in RunConfig

/// @brief Configuration parameters for executing an IL module.
struct RunConfig
{
    TraceConfig trace;                  ///< Tracing configuration.
    uint64_t maxSteps = 0;              ///< Step limit; zero disables the limit.
    DebugCtrl debug;                    ///< Debug controller copied into the VM.
    DebugScript *debugScript = nullptr; ///< Optional script pointer; not owned.
    std::vector<ExternDesc> externs;    ///< Pre-registered extern helpers.
    /// @brief Per-frame operand stack size in bytes.
    /// @details Controls the amount of stack storage available for @c alloca
    ///          operations within each function call. Defaults to 64KB which
    ///          suffices for typical BASIC programs. Larger values support
    ///          workloads with bigger local arrays; smaller values can be used
    ///          for memory-constrained environments or testing.
    std::size_t stackBytes = 65536;
    /// @brief Command-line arguments to seed into the runtime before run().
    /// @details When non-empty, the Runner seeds the runtime argument store
    ///          after VM construction so BASIC's ARGC/ARG$/COMMAND$ can read
    ///          them safely.
    std::vector<std::string> programArgs;

    // Periodic host polling --------------------------------------------------
    /// @brief Invoke a host callback every N instructions (0 disables).
    uint32_t interruptEveryN = 0;
    /// @brief Host callback; return false to request a VM pause.
    std::function<bool(VM &)> pollCallback;
};

/// @brief Lightweight façade owning a VM instance for running IL modules.
class Runner
{
  public:
    /// @brief Construct a runner over @p module with optional @p config.
    /// @details Builds a VM instance, applies tracing/debug config, and seeds externs/args.
    /// @param module IL module to execute. Must remain valid for the Runner's lifetime.
    /// @param config Optional configuration controlling tracing, debugging, and step limits.
    Runner(const il::core::Module &module, RunConfig config = {});

    /// @brief Destroy the runner and release owned VM resources.
    /// @details Destroys the pimpl instance which owns the underlying VM, ensuring
    ///          clean shutdown of tracing, debug, and runtime bridges.
    ~Runner();

    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    Runner(Runner &&) noexcept;
    Runner &operator=(Runner &&) noexcept;

    /// @brief Execute the module's entry function.
    [[nodiscard]] int64_t run();

    /// @brief Retrieve the total number of instructions executed by the VM.
    [[nodiscard]] uint64_t instructionCount() const;

    /// @brief Retrieve the most recent trap message emitted by the VM, if any.
    [[nodiscard]] std::optional<std::string> lastTrapMessage() const;

    // Opcode counting facade

    /// @brief Read-only view of per-opcode execution counts.
    /// @return Reference to the internal array of per-opcode counters.
    [[nodiscard]] const std::array<uint64_t, il::core::kNumOpcodes> &opcodeCounts() const;

    /// @brief Reset all opcode execution counters to zero.
    void resetOpcodeCounts();

    /// @brief Return the top-N most executed opcodes and their counts.
    /// @param n Number of top entries to return.
    /// @return Vector of (opcode index, count) pairs sorted by count descending.
    [[nodiscard]] std::vector<std::pair<int, uint64_t>> topOpcodes(std::size_t n) const;

    // Extern registration facade

    /// @brief Register a foreign function helper for name-based resolution.
    /// @param ext Descriptor for the external function to register.
    void registerExtern(const ExternDesc &);

    /// @brief Remove a previously registered extern by name.
    /// @param name Canonical name of the extern to unregister.
    /// @return True if an entry was removed, false if not found.
    bool unregisterExtern(std::string_view name);

    //===------------------------------------------------------------------===//
    // Single-step and continue APIs
    //===------------------------------------------------------------------===//

    /// @brief Result status for a single VM step.
    enum class StepStatus
    {
        Advanced,      ///< Successfully executed one instruction; can continue.
        Halted,        ///< Program finished (returned from main).
        BreakpointHit, ///< Reached a breakpoint or step budget pause.
        Trapped,       ///< Unhandled trap occurred.
        Paused         ///< Paused for non-breakpoint reason (e.g., external pause).
    };

    /// @brief Payload returned by a single-step operation.
    struct StepResult
    {
        StepStatus status; ///< Final status for this step.
    };

    /// @brief Aggregate status reported by continueRun.
    enum class RunStatus
    {
        Halted,            ///< Program finished (returned from main).
        BreakpointHit,     ///< Hit a breakpoint while running.
        Trapped,           ///< Unhandled trap occurred.
        Paused,            ///< Paused for non-breakpoint reason.
        StepBudgetExceeded ///< Global step limit reached.
    };

    /// @brief Execute exactly one instruction of the program (initialising on first call).
    StepResult step();

    /// @brief Continue running until a terminal state (halt, trap, or breakpoint).
    RunStatus continueRun();

    /// @brief Set a source-line breakpoint using a concrete source location.
    void setBreakpoint(const il::support::SourceLoc &);

    /// @brief Clear all configured breakpoints.
    void clearBreakpoints();

    /// @brief Update the global step budget (0 disables the limit).
    void setMaxSteps(uint64_t);

    /// @brief Light-weight snapshot of the last trap for diagnostics.
    ///
    /// @details Populated when a trap occurs during execution. Use lastTrap()
    ///          to retrieve this information after RunStatus::Trapped is returned.
    ///
    /// Trap kinds (matching TrapKind enum values):
    /// - 0: DivideByZero - Integer division or remainder by zero
    /// - 1: Overflow - Arithmetic or conversion overflow
    /// - 2: InvalidCast - Invalid cast or conversion semantics
    /// - 3: DomainError - Semantic domain violation or user trap
    /// - 4: Bounds - Array bounds check failure
    /// - 5: FileNotFound - File system open on non-existent path
    /// - 6: EOF - End-of-file reached while input still expected
    /// - 7: IOError - Generic I/O failure
    /// - 8: InvalidOperation - Operation outside allowed state machine
    /// - 9: RuntimeError - Catch-all for unexpected runtime failures
    ///
    /// @note A trap of kind RuntimeError with a message containing "unimplemented
    ///       opcode" indicates a fatal programmer error (unknown/missing opcode
    ///       handler). This is not recoverable; see the header documentation for
    ///       details on unknown opcode handling.
    struct TrapInfo
    {
        int32_t kind = 0;     ///< Trap kind (see enum values above).
        int32_t code = 0;     ///< Secondary error code (0 = none).
        uint64_t ip = 0;      ///< Instruction index within block at trap.
        int32_t line = -1;    ///< Source line (-1 = unknown).
        std::string function; ///< Function name (empty if unknown).
        std::string block;    ///< Block label (empty if unknown).
        std::string message;  ///< Formatted human-readable trap message.
    };

    /// @brief Retrieve a pointer to the last trap snapshot, if any; nullptr otherwise.
    const TrapInfo *lastTrap() const;

    // Memory watch façade ----------------------------------------------------
    /// @brief Register a memory watch range with a tag.
    void addMemWatch(const void *addr, std::size_t size, std::string tag);

    /// @brief Remove a previously registered memory watch range.
    bool removeMemWatch(const void *addr, std::size_t size, std::string_view tag);

    /// @brief Drain pending memory watch hit payloads.
    std::vector<MemWatchHit> drainMemWatchHits();

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

/// @brief Convenience helper to run @p module with @p config and return the exit code.
[[nodiscard]] int64_t runModule(const il::core::Module &module, RunConfig config = {});

} // namespace il::vm
