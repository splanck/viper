# ADR 0002: Shared-Memory Threads with FIFO Monitors and Safe Variables

Date: 2025-12-18

Context

Viper currently supports concurrency at the host/embedder level by running multiple VM instances in parallel (one VM per
thread). This avoids synchronization overhead in the interpreter but does not provide language-level threading for Viper
programs. The runtime was also designed around single-threaded execution: heap reference counting is not atomic, some
runtime subsystems rely on process-global state, and the native backend initializes a legacy runtime context only on the
main thread.

We want to add a modern `Viper.Threads` namespace that works in:

- the VM, and
- both native codegen backends,

with shared memory and explicit locking. Additionally, we want “safe variables”: special runtime types whose operations
are automatically thread-safe and fair (FIFO), so programs can avoid ad hoc locking for common patterns.

Viper also requires determinism between the VM and native backends for defined programs. OS thread scheduling is not
deterministic, so we must define which threaded programs are “defined” and what guarantees Viper provides to preserve
repeatability.

Decision

Add `Viper.Threads` with:

1. Shared-memory OS threads in VM and native.
2. A re-entrant, FIFO-fair monitor primitive (`Viper.Threads.Monitor`) for explicit object locking.
3. FIFO-safe variable types (`Viper.Threads.Safe*`) that automatically serialize access.
4. A separate runtime component library (`viper_rt_threads`) that is linked only when required by the program (to avoid
   bloating non-threaded binaries).

Definitions

- Defined threaded program: A program is defined if all shared mutable state is accessed under `Monitor` (or via `Safe*`
  types). Programs with data races are undefined (VM/native equivalence is not required for undefined programs).
- FIFO fairness: For contended operations, arrival order determines service order. This applies to:
  - monitor lock acquisition, and
  - monitor waiters awakened via `Pause`/`PauseAll`.
- Re-entrant monitor: The thread currently owning the monitor may `Enter` again without blocking; the monitor tracks a
  recursion count and requires the matching number of `Exit` calls.

API Surface (v1)

Monitor (static)

- `Viper.Threads.Monitor.Enter(obj) -> void`
- `Viper.Threads.Monitor.TryEnter(obj) -> i1`
- `Viper.Threads.Monitor.TryEnterFor(obj, i64 ms) -> i1`
- `Viper.Threads.Monitor.Exit(obj) -> void`
- `Viper.Threads.Monitor.Wait(obj) -> void`
- `Viper.Threads.Monitor.WaitFor(obj, i64 ms) -> i1`
- `Viper.Threads.Monitor.Pause(obj) -> void`
- `Viper.Threads.Monitor.PauseAll(obj) -> void`

Thread (object)

- `Viper.Threads.Thread.Start(ptr entry, ptr arg) -> obj`
- `Viper.Threads.Thread.Join(obj) -> void`
- `Viper.Threads.Thread.TryJoin(obj) -> i1`
- `Viper.Threads.Thread.JoinFor(obj, i64 ms) -> i1`
- `Viper.Threads.Thread.get_Id(obj) -> i64`
- `Viper.Threads.Thread.get_IsAlive(obj) -> i1`
- `Viper.Threads.Thread.Sleep(i64 ms) -> void`
- `Viper.Threads.Thread.Yield() -> void`

Safe Variables (FIFO by construction)

Representative v1 set (expand later):

- `Viper.Threads.SafeI64.New(i64 initial) -> obj`
- `Viper.Threads.SafeI64.Get(obj) -> i64`
- `Viper.Threads.SafeI64.Set(obj, i64) -> void`
- `Viper.Threads.SafeI64.Add(obj, i64 delta) -> i64`
- `Viper.Threads.SafeI64.CompareExchange(obj, i64 expected, i64 desired) -> i64`

Also planned in the same design:

- `SafeF64`, `SafeStr`, `SafeObj` with analogous operations.

Semantics

Monitor.Enter/Exit

- `Enter(obj)`:
  - traps on null `obj`,
  - if the calling thread already owns the monitor, increments recursion and returns immediately,
  - otherwise enqueues the calling thread in the monitor’s FIFO acquisition queue and blocks until it becomes the owner.
- `Exit(obj)`:
  - traps on null `obj`,
  - traps if the calling thread is not the current owner,
  - decrements recursion; when recursion reaches zero, transfers ownership to the next FIFO waiter (if any).

Monitor.TryEnter/TryEnterFor

- `TryEnter(obj) -> i1`:
  - returns 1 if acquired (or re-entered),
  - returns 0 if contended,
  - traps on null `obj`.
- `TryEnterFor(obj, ms) -> i1`:
  - blocks up to `ms` milliseconds attempting to acquire fairly,
  - returns 1 if acquired; otherwise returns 0 on timeout,
  - traps on null `obj`.

Monitor.Wait/WaitFor and Pause/PauseAll

These operations provide condition-variable-like behavior associated with the monitor object.

- Precondition: the calling thread must own the monitor; otherwise trap.
- `Wait(obj)`:
  - releases the monitor (fully, restoring recursion on re-acquire),
  - enqueues the thread on the monitor’s FIFO wait queue,
  - blocks until it is awakened by `Pause`/`PauseAll`,
  - then re-acquires the monitor (via the same FIFO discipline) and returns.
- `WaitFor(obj, ms) -> i1`:
  - like `Wait`, but returns 0 if the timeout elapses before being awakened,
  - returns 1 if awakened and the monitor was successfully re-acquired before returning.
- `Pause(obj)`:
  - requires ownership; otherwise trap,
  - wakes the oldest waiter (if any) from the FIFO wait queue.
- `PauseAll(obj)`:
  - requires ownership; otherwise trap,
  - wakes all waiters in FIFO order.

Wake-ups are FIFO-preserving: `Pause`/`PauseAll` selects waiters in FIFO order, and awakened waiters contend for the lock
in the same order they were awakened.

Thread.Start and function pointers

`Thread.Start(entry, arg)` starts a new OS thread that will invoke `entry(arg)` and then terminate. The `entry` pointer is
backend-defined:

- Native: `entry` is a raw code pointer with the C ABI signature `void (*)(void *)`.
- VM: `entry` is a VM function pointer (currently represented as an `il::core::Function *`) and is invoked by a per-thread
  interpreter runner.

Determinism and Repeatability

- FIFO fairness is guaranteed for monitor queues, providing repeatability for many data-race-free programs.
- Viper does not attempt to control OS scheduling. If a program is data-race-free but still depends on timing (e.g., busy
  loops without synchronization), the program is considered undefined for VM/native equivalence.

Error Handling (Exact Trap Messages)

Monitor:

- `Monitor.Enter: null object`
- `Monitor.Exit: null object`
- `Monitor.Exit: not owner`
- `Monitor.Wait: not owner`
- `Monitor.Pause: not owner`
- `Monitor.PauseAll: not owner`

Thread:

- `Thread.Start: null entry`
- `Thread.Start: failed to create thread`
- `Thread.Join: null thread`
- `Thread.Join: already joined`
- `Thread.Join: cannot join self`

Implementation Strategy

Runtime (shared by VM + native)

- Make heap reference counting atomic (required for sharing strings/arrays/objects between threads).
- Implement a monitor table keyed by object pointer, with explicit FIFO queues for lock acquisition and waiters.
- Provide `Safe*` types implemented as runtime objects that use the same monitor mechanism internally.
- Implement OS thread creation and a trampoline that binds an appropriate runtime context on the new thread.

Native backends

- Add `viper_rt_threads` as a separate runtime component and link it only when a program references `rt_thread_*`,
  `rt_monitor_*`, or `rt_safe_*` symbols.
- Ensure platform linker flags are added only when threads are required (e.g., `-pthread` on Linux).

VM

- Introduce a “program instance” concept that owns shared mutable globals storage and a shared runtime context.
- Each VM thread runs its own interpreter state but shares the program instance so heap objects and globals are truly
  shared across threads.
- Implement `Viper.Threads.Thread.*` via the VM extern registry override mechanism so VM threads can invoke VM function
  pointers directly.

Consequences

Pros:

- Adds modern concurrency primitives to Viper with explicit, predictable fairness semantics.
- Enables VM/native parity for synchronized (defined) threaded programs.
- Keeps non-threaded native binaries small by linking threading support only when used.

Cons:

- Atomic refcounting and FIFO fairness add overhead compared to single-threaded execution.
- Introduces deadlock risk (as in all lock-based models); tooling and docs will need to guide correct usage.
- Requires substantial VM refactoring to share program state safely across interpreter threads.

Alternatives

- Cooperative (green) threads in the VM (would not match native OS threads and would complicate interoperability).
- Native-only threads with VM trapping (rejected; VM must support threads).
- Non-FIFO locks relying on OS scheduling (rejected; FIFO fairness is required for repeatability).
- Per-object embedded mutex/condition fields (rejected; would bloat every heap object and constrain object layout).

Spec Impact

- Adds new runtime namespace and runtime symbols; does not add new IL opcodes.
- Requires documenting the definition of “defined” threaded programs (data-race-free, synchronization-based) for VM/native
  equivalence.

Migration Plan

1. Add atomic refcounting and thread-safe runtime context initialization; add unit tests for concurrent retain/release.
2. Add `viper_rt_threads` with FIFO `Monitor` and `SafeI64`; add unit tests for FIFO ordering, re-entrancy, and
   `Wait/Pause/PauseAll`.
3. Add native `Thread.Start/Join` with per-thread runtime context binding; integrate into both native backends.
4. Add VM program-instance shared state and a VM thread runner; implement `Thread.*` via extern overrides.
5. Expand `Safe*` coverage and add frontend syntax/lowering for “safe variables”.
6. Add VM-vs-native end-to-end tests restricted to defined threaded programs.

