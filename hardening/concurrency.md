# Concurrency Audit — Viper Runtime & Compiler

- **Date:** 2026-03-05
- **Scope:** All multi-threaded code in `src/runtime/`, `src/vm/`, `src/il/`, `src/codegen/`

---

## Methodology

Every `.c`, `.cpp`, and `.h` file in scope was searched for `volatile`, `atomic`,
`mutex`, `pthread`, `Critical`, `Interlocked`, `__atomic_`, `std::atomic`,
`spinlock`, `lock`, and `thread`. Each synchronization primitive was traced for
correct pairing (init/lock/unlock), correct memory ordering, and absence of
time-of-check-to-time-of-use (TOCTOU) windows. Safe patterns are catalogued
separately to demonstrate full coverage.

---

## Findings Summary

| ID | Severity | File | Line(s) | Category | Description |
|----|----------|------|---------|----------|-------------|
| CONC-001 | HIGH | `rt_gc.c` | 128-138 | Init race | `gc_lock_init()` checks+sets `g_gc.lock_init` without atomics; concurrent double-init corrupts mutex |
| CONC-002 | HIGH | `rt_string_intern.c` | 88-96 | Init race (Win) | `g_lock_init_` checked without atomics; two threads can double-init `CRITICAL_SECTION` |
| CONC-003 | MEDIUM | `rt_gc.c` | 650-661 | TOCTOU | `rt_gc_notify_alloc()` threshold check + counter reset not atomic as compound op; benign double-collect |
| CONC-004 | MEDIUM | `rt_pool.c` | 116-119, 132-135 | Wrong barrier (ARM64) | `atomic_load/store_u64` use `_ReadWriteBarrier()` (compiler-only) under `_M_ARM64`; needs CPU fence |
| CONC-005 | MEDIUM | `rt_pool.c` | 147-150 | Misleading volatile | Pool stats `volatile` but all accesses use `__atomic_*`; `volatile` obscures real synchronization |
| CONC-006 | MEDIUM | `rt_parallel.c` | 74-154 | Misleading volatile | `remaining` counters `volatile` but protected by mutex / `Interlocked*`; `volatile` is redundant |
| CONC-007 | LOW | `rt_stack_safety.c` | 49, 99 | Init race (benign) | `g_stack_safety_initialized` checked without atomics; double-init is idempotent |
| CONC-008 | LOW | `rt_audio.c` | 58 | Non-standard spinlock | Hand-rolled `volatile int` spinlock; `pthread_once`/`InitOnceExecuteOnce` preferred |
| CONC-009 | LOW | `VM.hpp` | 539-544 | Unguarded shared state | `ProgramState` maps shared across VMs via `shared_ptr` without locking |
| CONC-010 | LOW | `rt_context.c` | 110 | Spinlock without backoff | `g_legacy_handoff_lock` pure-spin; wastes CPU under contention |

---

## Detailed Findings

### CONC-001: GC lock initialization race

- **Severity:** HIGH
- **Category:** Init race
- **Location:** `src/runtime/core/rt_gc.c:128-138`
- **Platforms:** All (Windows, macOS, Linux)

`gc_lock_init()` reads `g_gc.lock_init` without atomics, then initializes the
mutex and sets the flag. If two threads call any GC function for the first time
concurrently, both can see `lock_init == 0`, both call
`pthread_mutex_init()`/`InitializeCriticalSection()`, and the second call
overwrites the first, corrupting the mutex state.

**Impact:** Mutex corruption can cause deadlocks or undefined behavior on any
subsequent `gc_lock()`/`gc_unlock()` call.

**Recommended fix:** Use `pthread_once` (POSIX) or `InitOnceExecuteOnce`
(Windows) for one-time initialization. Alternatively, on POSIX, use
`PTHREAD_MUTEX_INITIALIZER` for static initialization (no runtime init needed).

---

### CONC-002: String intern lock initialization race (Windows)

- **Severity:** HIGH
- **Category:** Init race
- **Location:** `src/runtime/core/rt_string_intern.c:88-96`
- **Platforms:** Windows only (POSIX side uses `PTHREAD_MUTEX_INITIALIZER`)

`intern_lock()` on Windows checks `g_lock_init_` without atomics, then calls
`InitializeCriticalSection()`. Two threads entering `intern_lock()` for the first
time can both initialize the `CRITICAL_SECTION`, corrupting it.

**Impact:** Same as CONC-001 — mutex corruption leading to deadlocks or
undefined behavior.

**Recommended fix:** Use `InitOnceExecuteOnce` with a static `INIT_ONCE` guard,
matching the pattern already used in `rt_monitor.c` and `rt_regex.c`.

---

### CONC-003: GC auto-trigger TOCTOU

- **Severity:** MEDIUM
- **Category:** TOCTOU
- **Location:** `src/runtime/core/rt_gc.c:650-661`
- **Platforms:** All

`rt_gc_notify_alloc()` performs three individually-atomic operations that are not
composed atomically: (1) load threshold, (2) increment counter via
`__atomic_fetch_add`, (3) if counter >= threshold, reset counter to 0 and call
`rt_gc_collect()`. Two threads can both pass the threshold check, both reset the
counter, and both trigger collection.

**Impact:** Occasional redundant GC pass (wasted work). No data corruption —
`rt_gc_collect()` acquires the GC mutex internally.

**Recommended fix:** Use a CAS loop to atomically claim the counter reset:
only the thread that successfully CASes `counter` from `>=threshold` to `0`
triggers collection. Alternatively, document the benign double-collect and accept
the minor overhead.

---

### CONC-004: ARM64 Windows missing CPU barrier in pool atomics

- **Severity:** MEDIUM
- **Category:** Wrong barrier
- **Location:** `src/runtime/core/rt_pool.c:116-119` (`atomic_load_u64`),
  `src/runtime/core/rt_pool.c:132-135` (`atomic_store_u64`)
- **Platforms:** ARM64 Windows only (x86-64 unaffected due to TSO)

Under `#if defined(_M_X64) || defined(_M_ARM64)`, both `atomic_load_u64` and
`atomic_store_u64` use `_ReadWriteBarrier()`, which is a compiler-only barrier
with no CPU fence effect. On x86-64 this is sufficient because the TSO memory
model makes aligned 8-byte loads/stores naturally ordered. On ARM64, the weak
memory model means loads can be reordered past stores without a CPU fence.

**Impact:** Lock-free freelist operations in the pool allocator could observe
stale data on ARM64 Windows, causing use-after-free or double-free of pool
blocks. This is a latent bug — it only triggers if Viper is built for ARM64
Windows (e.g., Snapdragon X Elite devices).

**Recommended fix:** Under `_M_ARM64`, replace `_ReadWriteBarrier()` with
`__dmb(_ARM64_BARRIER_ISH)` after loads (for acquire) and before stores (for
release). Alternatively, use `_InterlockedCompareExchange64` universally for both
load and store paths (it implies a full barrier on all architectures).

---

### CONC-005: Redundant `volatile` on pool statistics

- **Severity:** MEDIUM
- **Category:** Misleading volatile
- **Location:** `src/runtime/core/rt_pool.c:147-150`
- **Platforms:** All

`rt_pool_state` declares `freelist_tagged`, `slabs`, `allocated`, and
`free_count` as `volatile`, but every access to these fields already uses
`__atomic_*` builtins or CAS operations. The `volatile` qualifier is redundant
(atomics provide their own compiler barrier) and obscures the actual
synchronization model.

**Impact:** No functional bug, but developers may incorrectly assume `volatile`
is the synchronization mechanism, leading to future bugs if they add non-atomic
accesses.

**Recommended fix:** Remove `volatile` qualifiers from `rt_pool_state` fields.
The `__atomic_*` builtins handle both compiler reordering and CPU ordering.

---

### CONC-006: Redundant `volatile` on parallel remaining counters

- **Severity:** MEDIUM
- **Category:** Misleading volatile
- **Location:** `src/runtime/threads/rt_parallel.c:74-154`
- **Platforms:** All

Task context structs declare `remaining` as `volatile LONG *` (Windows) or
`volatile int *` (POSIX). On Windows, `InterlockedDecrement` provides full
barrier semantics regardless of `volatile`. On POSIX, the decrement is protected
by `pthread_mutex_lock`/`unlock`, which provides the required ordering.

**Impact:** Same as CONC-005 — no functional bug, but misleading.

**Recommended fix:** Remove `volatile` qualifiers. The mutex (POSIX) and
Interlocked intrinsics (Windows) already guarantee memory ordering.

---

### CONC-007: Stack safety init race (benign)

- **Severity:** LOW
- **Category:** Init race (benign)
- **Location:** `src/runtime/core/rt_stack_safety.c:49` (Windows),
  `src/runtime/core/rt_stack_safety.c:99` (POSIX)
- **Platforms:** All

`g_stack_safety_initialized` is declared `volatile int` and checked without
atomic operations. Two threads could both see `0` and both register the
signal/VEH handler.

**Impact:** None — re-registering a signal handler (`sigaction`) or adding
another VEH handler (`AddVectoredExceptionHandler`) is idempotent.

**Recommended fix:** Optionally use `__atomic_load_n`/`__atomic_store_n` for
correctness hygiene, or use `pthread_once`/`InitOnceExecuteOnce`. Low priority.

---

### CONC-008: Non-standard audio init spinlock

- **Severity:** LOW
- **Category:** Non-standard spinlock
- **Location:** `src/runtime/audio/rt_audio.c:58`
- **Platforms:** All

Audio initialization uses a hand-rolled spinlock via `volatile int
g_audio_init_lock` with atomic test-and-set. While functionally correct (the
double-checked locking pattern with ACQUIRE/RELEASE is properly implemented),
this is non-standard and harder to audit than OS primitives.

**Impact:** No functional bug. Under high contention during init (unlikely in
practice), the pure-spin loop wastes CPU.

**Recommended fix:** Replace with `pthread_once` (POSIX) or
`InitOnceExecuteOnce` (Windows) for the init path. The existing pattern works
but is harder to maintain.

---

### CONC-009: ProgramState shared across VMs without locking

- **Severity:** LOW
- **Category:** Unguarded shared state
- **Location:** `src/vm/VM.hpp:539-544`
- **Platforms:** All

`ProgramState` contains `StrMap` (`std::unordered_map<string_view,
ViperStringHandle>`) and `MutableGlobalMap` (`std::unordered_map<string_view,
void*>`) — neither has internal locking. `ProgramState` is shared across VM
instances via `std::shared_ptr`. If two VMs sharing the same `ProgramState` are
executed concurrently on different threads, they can race on map insertions and
lookups.

**Impact:** Data corruption of the string or global maps. Currently mitigated by
design — `ThreadsRuntime.cpp` creates separate VM instances per thread, and the
shared `ProgramState` is only populated during module initialization (before
threads start). The invariant is not enforced in code.

**Recommended fix:** Add a debug assertion in `VM::run()` or `ActiveVMGuard`
that verifies no other thread is concurrently using the same `ProgramState`, or
document the invariant prominently. For full safety, add a `shared_mutex` to
`ProgramState` with reader locks in `run()` and writer locks during init.

---

### CONC-010: Context handoff spinlock without backoff

- **Severity:** LOW
- **Category:** Spinlock without backoff
- **Location:** `src/runtime/core/rt_context.c:110`
- **Platforms:** All

`g_legacy_handoff_lock` is a pure spinlock implemented via
`__atomic_test_and_set`/`__atomic_clear`. When contended, threads spin without
yielding or backing off, consuming CPU time.

**Impact:** Wasted CPU under contention. In practice, contention is rare (only
during VM context binding/unbinding transitions), so the impact is minimal.

**Recommended fix:** Add exponential backoff with `sched_yield()` (POSIX) or
`SwitchToThread()` (Windows) after a few spin iterations. Alternatively, replace
with a mutex if the critical section is long enough to justify it.

---

## Verified Safe Patterns

The following patterns were audited and confirmed correct:

- **String interning (POSIX):** `PTHREAD_MUTEX_INITIALIZER` static init — no runtime race
- **Monitor table:** `INIT_ONCE` (Windows) / `PTHREAD_MUTEX_INITIALIZER` (POSIX) — correct lazy init
- **Pool allocator CAS:** Lock-free with tagged pointers for ABA prevention — correct
- **Thread ID generation:** `__atomic_fetch_add` / `InterlockedIncrement64` — monotonic, correct
- **VM interrupt flag:** `std::atomic<bool>` with `memory_order_relaxed` — appropriate for polled flag
- **Signal handlers:** Only `std::atomic::store()` (async-signal-safe) — correct
- **ExternRegistry:** `std::mutex` with `std::lock_guard` — no lock inversion, correct
- **AnalysisManager:** `std::shared_mutex` reader-writer pattern — correct fast-path for cache hits
- **Condition variables:** All use `while`-loop predicate checks — spurious wakeups handled
- **Thread-local storage:** Comprehensive use (`RT_THREAD_LOCAL`) for per-thread context isolation
- **Output batch mode:** `__atomic_fetch_add`/`__atomic_fetch_sub` on `g_batch_mode_depth` — correct
- **Cancellation tokens:** `atomic_int` (POSIX) / `volatile LONG` with `Interlocked*` (Windows) — correct
- **Thread primitives:** Explicit `mutex.unlock()` before `rt_trap()` in `rt_threads_primitives.cpp` — prevents longjmp-induced deadlock
- **Network/TLS:** Per-connection state, no global mutable state — thread-safe by design
- **Regex cache:** `INIT_ONCE` / `pthread_mutex` — correct lazy init (same pattern as monitor table)

---

## Fix Status

All 10 findings have been fixed. Tests: `src/tests/runtime/RTConcurrencyTests.cpp`.

| IDs | Status | Fix Applied |
|-----|--------|-------------|
| CONC-001, CONC-002 | **FIXED** | `PTHREAD_MUTEX_INITIALIZER` (POSIX) / `InitOnceExecuteOnce` (Windows) |
| CONC-003 | **FIXED** | CAS loop for atomic counter reset in `rt_gc_notify_alloc()` |
| CONC-004 | **FIXED** | `__dmb(_ARM64_BARRIER_ISH)` under `_M_ARM64` in `rt_pool.c` and `rt_platform.h` |
| CONC-005 | **FIXED** | Removed `volatile` from `rt_pool_state` fields |
| CONC-006 | **FIXED** | Removed `volatile` from `parallel_sync` and task struct `remaining` counters |
| CONC-007 | **FIXED** | `__atomic_load_n` / `__atomic_store_n` for `g_stack_safety_initialized` |
| CONC-008 | **FIXED** | Added `sched_yield()` / `SwitchToThread()` in audio init spin loop |
| CONC-009 | **FIXED** | `std::atomic<bool> initComplete` in `ProgramState` + debug assert on sharing |
| CONC-010 | **FIXED** | Added `sched_yield()` / `SwitchToThread()` in context handoff spinlock |
