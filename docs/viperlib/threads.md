---
status: active
audience: public
last-verified: 2026-05-08
---

# Threads

> Shared-memory threading primitives with FIFO fairness.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Threads.Thread](#viperthreadsthread)
- [Viper.Threads.Monitor](#viperthreadsmonitor)
- [Viper.Threads.SafeI64](#viperthreadssafei64)
- [Viper.Threads.Gate](#viperthreadsgate)
- [Viper.Threads.Barrier](#viperthreadsbarrier)
- [Viper.Threads.RwLock](#viperthreadsrwlock)
- [Viper.Threads.Pool](#viperthreadspool)
- [Viper.Threads.Promise](#viperthreadspromise)
- [Viper.Threads.Future](#viperthreadsfuture)
- [Viper.Threads.Parallel](#viperthreadsparallel)
- [Viper.Threads.CancelToken](#viperthreadscanceltoken)
- [Viper.Threads.Debouncer](#viperthreadsdebouncer)
- [Viper.Threads.Throttler](#viperthreadsthrottler)
- [Viper.Threads.Scheduler](#viperthreadsscheduler)
- [Viper.Threads.Async](#viperthreadsasync)
- [Viper.Threads.ConcurrentMap](#viperthreadsconcurrentmap)
- [Viper.Threads.ConcurrentQueue](#viperthreadsconcurrentqueue)
- [Viper.Threads.Channel](#viperthreadschannel)

---

## Viper.Threads.Thread

OS threads for Viper programs (VM and native backends).

**Type:** Instance class (created by `Start`)

### Methods

| Method                    | Signature                       | Description                                |
|--------------------------|----------------------------------|--------------------------------------------|
| `Join()`                 | `Void()`                         | Wait until the thread finishes             |
| `JoinFor(ms)`            | `Boolean(Integer)`               | Join with timeout in milliseconds          |
| `SafeGetId()`            | `Integer()`                      | Get the ID of a safe thread handle         |
| `SafeIsAlive()`          | `Boolean()`                      | Check if a safe thread is still running    |
| `SafeJoin()`             | `Void()`                         | Join a safe thread handle                  |
| `Sleep(ms)`              | `Void(Integer)`                  | Sleep the current thread (ms, clamped)     |
| `Start(entry, arg)`      | `Thread(Ptr, Ptr)`               | Start a new thread running `entry(arg)`    |
| `StartOwned(entry, arg)` | `Thread(Ptr, Object)`            | Start a new thread and retain a runtime object argument until the entry returns |
| `StartSafe(entry, arg)`  | `Thread(Ptr, Ptr)`               | Start a thread with error boundaries; traps are captured instead of crashing |
| `StartSafeOwned(entry, arg)` | `Thread(Ptr, Object)`        | Safe-thread variant of `StartOwned`        |
| `TryJoin()`              | `Boolean()`                      | Non-blocking join attempt                  |
| `Yield()`                | `Void()`                         | Yield the current thread's time slice      |

### Properties

| Property   | Type                  | Description                            |
|------------|-----------------------|----------------------------------------|
| `Id`       | `Integer` (read-only) | Monotonic thread id (1, 2, 3, …)       |
| `IsAlive`  | `Boolean` (read-only) | True while entry function is running   |
| `HasError` | `Boolean` (read-only) | True if a safe thread exited with a trap (only for `StartSafe` threads) |
| `Error`    | `String` (read-only)  | Error message if the safe thread trapped (empty otherwise) |

### Entry Function

`Start(entry, arg)` expects `entry` to be a function pointer with one of these signatures:

- `void()` (no args), or
- `void(ptr)` (one `ptr` argument).

In IL, obtain an entry pointer via `addr_of`:

```text
func @worker(ptr %arg) -> void { ... }

func @main() -> i64 {
entry:
  %fn = addr_of @worker
  %t  = call @Viper.Threads.Thread.Start(%fn, const_null)
  call @Viper.Threads.Thread.Join(%t)
  ret 0
}
```

**Backend notes:**

- **Native:** `entry` is a raw code pointer with C ABI `void (*)(void *)`.
- **VM:** `entry` is a VM function pointer (internally `il::core::Function*`) and is invoked by a per-thread VM runner.
- Use `StartOwned` / `StartSafeOwned` when `arg` is a runtime-managed object or string handle that should stay alive until the callback returns.

### Safe Threads (Error Boundaries)

`StartSafe(entry, arg)` is like `Start` but wraps the thread entry in a trap recovery context
using `setjmp`/`longjmp`. If the entry function triggers a trap (null dereference, bounds check
failure, etc.), the trap is captured instead of crashing the process.

This applies uniformly to VM-backed and BytecodeVM-backed worker functions. A
trapped worker marks the safe thread handle as failed instead of silently
completing. VM and BytecodeVM workers route both runtime-call traps and
interpreter traps through the same safe-thread boundary.

After the thread finishes, check `HasError` and `Error` on the returned handle:

```rust
var t = Viper.Threads.Thread.StartSafe(&worker, 0);
Viper.Threads.Thread.Sleep(500);   // wait for thread
if (t.HasError) {
    Say("Thread trapped: " + t.Error);
} else {
    Say("Thread completed normally");
}
```

`SafeJoin()`, `SafeGetId()`, and `SafeIsAlive()` remain available for explicit
safe-thread handling. Standard `Join()`, `TryJoin()`, `JoinFor()`, `Id`, and
`IsAlive` also accept safe-thread handles and delegate to the underlying worker.
The safe-specific methods also accept regular thread handles.

**Use cases:**
- Database servers: isolate per-connection errors so one bad query doesn't crash all sessions
- Plugin systems: run untrusted code without risking process stability
- Test runners: run tests in threads and capture failures

### Join Timeouts

`JoinFor(ms)` behaves as:

| `ms` value | Behavior                         |
|------------|----------------------------------|
| `< 0`      | Wait indefinitely (same as Join) |
| `= 0`      | Immediate check (same as TryJoin)|
| `> 0`      | Wait up to `ms` milliseconds     |

A successful `Join()`, a `TryJoin()` that returns true, or a `JoinFor()` that
returns true consumes the underlying OS join. Later join attempts on the same
handle trap with `Thread.Join: already joined`; query-only properties such as
`Id`, `IsAlive`, `HasError`, and `Error` remain usable.

### Errors (Traps)

- `Thread.Start: null entry`
- `Thread.Start: failed to create thread`
- `Thread.Join: null thread`
- `Thread.Join: cannot join self`
- `Thread.Join: already joined`

### Zia Example

> Thread requires function pointers (`addr_of`) for entry functions, which is an advanced Zia feature. See the IL example above or use BASIC `ADDR_OF` for thread creation.

### BASIC Example

```basic
' Sleep the current thread briefly
Viper.Threads.Thread.Sleep(10)
PRINT "Slept for 10ms"

' Yield the current thread's time slice
Viper.Threads.Thread.Yield()
PRINT "Yielded time slice"
```

---

## Viper.Threads.Monitor

FIFO-fair, re-entrant monitor for explicit object locking.

**Type:** Static utility class

### Methods

| Method                      | Signature                    | Description                                         |
|----------------------------|------------------------------|-----------------------------------------------------|
| `Enter(obj)`               | `Void(Object)`               | Acquire monitor (blocks, FIFO)                      |
| `Exit(obj)`                | `Void(Object)`               | Release monitor (balances `Enter`)                  |
| `Pause(obj)`               | `Void(Object)`               | Wake one waiter (FIFO)                              |
| `PauseAll(obj)`            | `Void(Object)`               | Wake all waiters (FIFO order, then FIFO re-acquire) |
| `TryEnter(obj)`            | `Boolean(Object)`            | Try to acquire immediately                          |
| `TryEnterFor(obj, ms)`     | `Boolean(Object, Integer)`   | Try to acquire with timeout (ms)                    |
| `Wait(obj)`                | `Void(Object)`               | Release monitor and wait for `Pause`/`PauseAll`     |
| `WaitFor(obj, ms)`         | `Boolean(Object, Integer)`   | Wait with timeout (returns false on timeout)        |

### Notes

- **Re-entrant:** The owning thread may `Enter` multiple times; recursion is tracked and must be matched by `Exit`.
- **FIFO:** Contended acquisition is FIFO; waiters awakened by `Pause`/`PauseAll` are moved to acquire in FIFO order.
- **Ownership required:** `Exit`, `Wait`, `WaitFor`, `Pause`, and `PauseAll` trap if the calling thread is not the owner.
- **Timeout units:** milliseconds. `TryEnterFor`/`WaitFor` treat negative values as `0` (immediate).
- **WaitFor always re-acquires:** even on timeout, `WaitFor` re-acquires the monitor before returning.

### Errors (Traps)

- `Monitor.Enter: null object`
- `Monitor.Exit: null object`
- `Monitor.Exit: not owner`
- `Monitor.Wait: not owner`
- `Monitor.Pause: not owner`
- `Monitor.PauseAll: not owner`

### Zia Example

> Monitor is a static utility class that operates on any object. Use `bind Viper.Threads.Monitor as Monitor;` and call `Monitor.Enter(obj)` / `Monitor.Exit(obj)`. Typically used with threads for synchronization.

### BASIC Example

```basic
' Create a simple object to use as monitor target
DIM obj AS OBJECT = Viper.Collections.Seq.New()

' Enter (acquire monitor)
Viper.Threads.Monitor.Enter(obj)
PRINT "Monitor acquired"

' TryEnter (re-entrant, same thread owns it)
DIM ok AS INTEGER = Viper.Threads.Monitor.TryEnter(obj)
PRINT "TryEnter: "; ok

' TryEnterFor (re-entrant with timeout)
DIM ok2 AS INTEGER = Viper.Threads.Monitor.TryEnterFor(obj, 10)
PRINT "TryEnterFor: "; ok2

' Exit to balance all three enters
Viper.Threads.Monitor.Exit(obj)
Viper.Threads.Monitor.Exit(obj)
Viper.Threads.Monitor.Exit(obj)
```

---

## Viper.Threads.SafeI64

FIFO-serialized “safe variable” for shared counters and flags.

**Type:** Instance class (requires `New(initial)`)

### Constructor

| Method        | Signature          | Description                  |
|---------------|--------------------|------------------------------|
| `New(initial)`| `SafeI64(Integer)` | Create a safe integer cell   |

### Methods

| Method                               | Signature                             | Description                                    |
|--------------------------------------|----------------------------------------|------------------------------------------------|
| `Add(delta)`                         | `Integer(Integer)`                     | Add and return new value                        |
| `CompareExchange(expected, desired)` | `Integer(Integer, Integer)`            | CAS; returns the value read before any update   |
| `Get()`                              | `Integer()`                            | Read current value                              |
| `Set(value)`                         | `Void(Integer)`                        | Write value                                     |

### Notes

- Operations are implemented by acquiring a FIFO monitor on the `SafeI64` object itself.
- You can also lock a `SafeI64` explicitly via `Monitor.Enter(cell)` when you need to group multiple operations.
- `Add` uses two's-complement wraparound on overflow instead of trapping.

### Errors (Traps)

- `SafeI64.New: alloc failed`
- `SafeI64.Get: null object`
- `SafeI64.Set: null object`
- `SafeI64.Add: null object`
- `SafeI64.CompareExchange: null object`

### Zia Example

```rust
module SafeI64Demo;

bind Viper.Terminal;
bind Viper.Threads.SafeI64 as SafeI64;
bind Viper.Fmt as Fmt;

func start() {
    var counter = SafeI64.New(0);
    Say("Initial: " + Fmt.Int(counter.Get()));

    counter.Set(42);
    Say("After Set: " + Fmt.Int(counter.Get()));

    var newVal = counter.Add(8);
    Say("After Add(8): " + Fmt.Int(newVal));    // 50

    var old = counter.CompareExchange(50, 100);
    Say("CAS old: " + Fmt.Int(old));            // 50
    Say("CAS result: " + Fmt.Int(counter.Get())); // 100
}
```

### BASIC Example

```basic
DIM counter AS OBJECT = Viper.Threads.SafeI64.New(0)
PRINT "Initial: "; counter.Get()

counter.Set(42)
PRINT "After Set: "; counter.Get()

DIM newVal AS INTEGER = counter.Add(8)
PRINT "After Add(8): "; newVal

DIM old AS INTEGER = counter.CompareExchange(50, 100)
PRINT "CAS old: "; old
PRINT "CAS result: "; counter.Get()
```

---

## Viper.Threads.Gate

FIFO-fair permit gate (semaphore concept).

**Type:** Instance class (requires `New(permits)`)

### Constructor

| Method         | Signature       | Description                            |
|---------------|------------------|----------------------------------------|
| `New(permits)` | `Gate(Integer)`  | Create a gate with `permits` available |

### Methods

| Method              | Signature            | Description                                          |
|--------------------|----------------------|------------------------------------------------------|
| `Enter()`          | `Void()`             | Acquire one permit (blocks, FIFO)                    |
| `Leave()`          | `Void()`             | Release one permit; wakes one waiter if present      |
| `Leave(count)`     | `Void(Integer)`      | Release `count` permits; wakes up to `count` waiters |
| `LeaveMany(count)` | `Void(Integer)`      | Signal `count` waiting goroutines to proceed (bulk signal); equivalent to calling Leave() count times but more efficient |
| `TryEnter()`       | `Boolean()`          | Try to acquire immediately                           |
| `TryEnterFor(ms)`  | `Boolean(Integer)`   | Try to acquire with timeout in milliseconds          |

### Properties

| Property    | Type                  | Description                    |
|-------------|-----------------------|--------------------------------|
| `Permits`   | `Integer` (read-only) | Current available permit count |

### Notes

- **FIFO:** Contended `Enter`/`TryEnterFor` acquisition is FIFO-fair.
- **Timeout units:** milliseconds. `TryEnterFor` treats negative values as `0` (immediate).
- `Leave`/`LeaveMany` trap instead of wrapping if the permit count would overflow.

### Errors (Traps)

- `Gate.New: permits cannot be negative`
- `Gate.Enter: null object`
- `Gate.TryEnter: null object`
- `Gate.TryEnterFor: null object`
- `Gate.Leave: null object`
- `Gate.Leave: count cannot be negative`
- `Gate.Leave: permit count overflow`

### Zia Example

```rust
module GateDemo;

bind Viper.Terminal;
bind Viper.Threads.Gate as Gate;
bind Viper.Fmt as Fmt;

func start() {
    var gate = Gate.New(3);
    Say("Permits: " + Fmt.Int(gate.get_Permits()));    // 3

    gate.Enter();
    Say("After Enter: " + Fmt.Int(gate.get_Permits())); // 2

    Say("TryEnter: " + Fmt.Bool(gate.TryEnter()));      // true
    Say("Permits: " + Fmt.Int(gate.get_Permits()));      // 1

    gate.Leave();
    gate.Leave();
}
```

> **Note:** Gate properties (`Permits`) use the get_/set_ pattern; access as `gate.get_Permits()` in Zia.

### BASIC Example

```basic
DIM gate AS OBJECT = Viper.Threads.Gate.New(3)
PRINT "Permits: "; gate.Permits

gate.Enter()
PRINT "After Enter: "; gate.Permits

PRINT "TryEnter: "; gate.TryEnter()
PRINT "Permits: "; gate.Permits

gate.Leave()
gate.Leave()
```

---

## Viper.Threads.Barrier

Reusable N-party barrier.

**Type:** Instance class (requires `New(parties)`)

### Constructor

| Method         | Signature          | Description                                 |
|---------------|--------------------|---------------------------------------------|
| `New(parties)` | `Barrier(Integer)` | Create a barrier for `parties` participants |

### Methods

| Method      | Signature     | Description                                                  |
|-------------|---------------|--------------------------------------------------------------|
| `Arrive()`  | `Integer()`   | Arrive and wait; returns arrival index `0..Parties-1`        |
| `Reset()`   | `Void()`      | Reset for reuse (traps if any threads are currently waiting) |

### Properties

| Property    | Type                  | Description              |
|-------------|-----------------------|--------------------------|
| `Parties`   | `Integer` (read-only) | Total parties required   |
| `Waiting`   | `Integer` (read-only) | Number currently waiting |

### Notes

- When all parties arrive, all are released simultaneously and the barrier resets.

### Errors (Traps)

- `Barrier.New: parties must be >= 1`
- `Barrier.Arrive: null object`
- `Barrier.Reset: null object`
- `Barrier.Reset: threads are waiting`

### Zia Example

```rust
module BarrierDemo;

bind Viper.Terminal;
bind Viper.Threads.Barrier as Barrier;
bind Viper.Fmt as Fmt;

func start() {
    var b = Barrier.New(3);
    Say("Parties: " + Fmt.Int(b.get_Parties()));  // 3
    Say("Waiting: " + Fmt.Int(b.get_Waiting()));  // 0
    // In a real program, multiple threads would call b.Arrive()
}
```

### BASIC Example

```basic
DIM b AS OBJECT = Viper.Threads.Barrier.New(3)
PRINT "Parties: "; b.Parties
PRINT "Waiting: "; b.Waiting

' With a single-party barrier we can demonstrate Arrive
DIM b1 AS OBJECT = Viper.Threads.Barrier.New(1)
DIM idx AS INTEGER = b1.Arrive()
PRINT "Arrive index: "; idx
```

---

## Viper.Threads.RwLock

Writer-preference reader-writer lock.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature  | Description       |
|---------|------------|-------------------|
| `New()` | `RwLock()` | Create a new lock |

### Methods

| Method             | Signature     | Description                                             |
|-------------------|---------------|---------------------------------------------------------|
| `ReadEnter()`      | `Void()`      | Acquire shared read lock (blocks on writer)             |
| `ReadExit()`       | `Void()`      | Release read lock                                       |
| `TryReadEnter()`   | `Boolean()`   | Non-blocking read acquire                               |
| `TryWriteEnter()`  | `Boolean()`   | Non-blocking write acquire                              |
| `WriteEnter()`     | `Void()`      | Acquire exclusive write lock (blocks on readers/writer) |
| `WriteExit()`      | `Void()`      | Release write lock                                      |

### Properties

| Property         | Type                   | Description              |
|------------------|------------------------|--------------------------|
| `Readers`        | `Integer` (read-only)  | Count of active readers  |
| `IsWriteLocked`  | `Boolean` (read-only)  | True if write lock held  |

### Notes

- **Writer preference:** new readers block while any writer is waiting.
- `ReadExit` must be called by the same thread that acquired the read lock.
- Read-to-write upgrades are not supported: `WriteEnter` traps while the current thread holds a read lock, and `TryWriteEnter` returns false.
- A thread that already owns the write lock may also enter the read side; this supports explicit write-to-read downgrade patterns when the matching `ReadExit` and `WriteExit` calls are balanced.

### Errors (Traps)

- `RwLock.ReadEnter: null object`
- `RwLock.ReadExit: null object`
- `RwLock.ReadExit: exit without matching enter`
- `RwLock.WriteEnter: null object`
- `RwLock.WriteEnter: cannot upgrade read lock`
- `RwLock.WriteExit: null object`
- `RwLock.WriteExit: exit without matching enter`
- `RwLock.WriteExit: not owner`

### Zia Example

```rust
module RwLockDemo;

bind Viper.Terminal;
bind Viper.Threads.RwLock as RwLock;
bind Viper.Fmt as Fmt;

func start() {
    var lock = RwLock.New();

    // Read lock (shared)
    lock.ReadEnter();
    Say("Readers: " + Fmt.Int(lock.get_Readers()));  // 1
    lock.ReadExit();

    // Write lock (exclusive)
    lock.WriteEnter();
    Say("IsWriteLocked: " + Fmt.Bool(lock.get_IsWriteLocked()));  // true
    lock.WriteExit();

    // Non-blocking try variants
    Say("TryRead: " + Fmt.Bool(lock.TryReadEnter()));   // true
    lock.ReadExit();
    Say("TryWrite: " + Fmt.Bool(lock.TryWriteEnter())); // true
    lock.WriteExit();
}
```

### BASIC Example

```basic
DIM lock AS OBJECT = Viper.Threads.RwLock.New()

' Read lock (shared)
lock.ReadEnter()
PRINT "Readers: "; lock.Readers
lock.ReadExit()

' Write lock (exclusive)
lock.WriteEnter()
PRINT "IsWriteLocked: "; lock.IsWriteLocked
lock.WriteExit()

' Non-blocking try variants
PRINT "TryRead: "; lock.TryReadEnter()
lock.ReadExit()
PRINT "TryWrite: "; lock.TryWriteEnter()
lock.WriteExit()
```

---

## Viper.Threads.Pool

Thread pool for submitting tasks to a fixed set of worker threads.

**Type:** Instance class (requires `New(size)`)

### Constructor

| Method       | Signature       | Description                                  |
|-------------|------------------|----------------------------------------------|
| `New(size)` | `Pool(Integer)`  | Create a pool with `size` worker threads (clamped to 1..1024) |

### Methods

| Method           | Signature             | Description                                                     |
|------------------|-----------------------|-----------------------------------------------------------------|
| `Submit(cb, arg)`| `Boolean(Ptr, Ptr)` | Submit a function pointer task for async execution; returns false if shut down  |
| `Wait()`         | `Void()`              | Block until all pending tasks complete                          |
| `WaitFor(ms)`    | `Boolean(Integer)`    | Wait with timeout; returns true if all tasks completed          |
| `Shutdown()`     | `Void()`              | Graceful shutdown: finish pending tasks, then stop workers      |
| `ShutdownNow()`  | `Void()`              | Immediate shutdown: discard queued tasks, stop workers          |

### Properties

| Property     | Type                   | Description                         |
|--------------|------------------------|-------------------------------------|
| `Size`       | `Integer` (read-only)  | Number of worker threads            |
| `Pending`    | `Integer` (read-only)  | Number of queued (not yet running) tasks |
| `Active`     | `Integer` (read-only)  | Number of tasks currently executing |
| `IsShutdown` | `Boolean` (read-only)  | True if the pool has been shut down |

### Notes

- Tasks are executed in FIFO order.
- `Submit` returns false after `Shutdown` or `ShutdownNow` has been called.
- `ShutdownNow` discards queued tasks; `Shutdown` allows them to finish.
- Calling `Wait`, `WaitFor`, `Shutdown`, or `ShutdownNow` from a worker in the same pool traps to prevent self-deadlock.
- Traps raised by a task do not leave the pool stuck in an active state. Once the pool drains, `Wait()` and successful `WaitFor(ms)` rethrow the last task trap instead of silently reporting success.
- Pool handles own their worker thread handles and release them after joins. Releasing a pool from one of its own workers requests shutdown and defers reclamation rather than freeing state out from under the running worker.

### Zia Example

> Pool requires function pointers (`addr_of`) for task callbacks. See the BASIC example or use `addr_of` in advanced Zia code.
> VM execution supports `Thread.Start` and `Async.Run` with VM-aware function pointers. VM-backed `Async.Run` retains a managed argument until the worker has consumed it. `Pool.Submit` still requires native callback pointers and traps when called from the VM with an IL function pointer.

### BASIC Example

```basic
' Create a pool with 4 worker threads
DIM pool AS OBJECT = Viper.Threads.Pool.New(4)
PRINT "Size: "; pool.Size         ' Output: 4
PRINT "IsShutdown: "; pool.IsShutdown  ' Output: 0

' Submit tasks (callback takes a single ptr argument)
SUB DoWork(arg AS PTR)
    ' Perform task work
    Viper.Threads.Thread.Sleep(10)
END SUB

pool.Submit(ADDR_OF DoWork, 0)
pool.Submit(ADDR_OF DoWork, 0)
pool.Submit(ADDR_OF DoWork, 0)

PRINT "Pending: "; pool.Pending   ' Output: up to 3
PRINT "Active: "; pool.Active     ' Output: up to 3

' Wait for all tasks to complete
pool.Wait()
PRINT "Pending after Wait: "; pool.Pending  ' Output: 0

' Graceful shutdown (drains queue before stopping)
pool.Shutdown()
PRINT "IsShutdown: "; pool.IsShutdown  ' Output: 1

' Submit returns false after shutdown
DIM ok AS INTEGER = pool.Submit(ADDR_OF DoWork, 0)
PRINT "Submit after shutdown: "; ok   ' Output: 0

' WaitFor with timeout
DIM pool2 AS OBJECT = Viper.Threads.Pool.New(2)
pool2.Submit(ADDR_OF DoWork, 0)
DIM done AS INTEGER = pool2.WaitFor(5000)  ' Wait up to 5 seconds
PRINT "Completed in time: "; done
pool2.Shutdown()
```

---

## Viper.Threads.Promise

Producer side of a Future/Promise pair for asynchronous result passing between threads.

**Type:** Instance class

A Promise creates a linked Future object. The Promise is used by the producer thread to set the result
value (or error), and the Future is used by the consumer thread to retrieve the result.

### Constructor

| Method  | Signature    | Description        |
|---------|--------------|-------------------|
| `New()` | `Promise()`  | Create a new Promise |

### Methods

| Method           | Signature           | Description                                      |
|------------------|---------------------|--------------------------------------------------|
| `GetFuture()`    | `Future()`          | Get the linked Future (always returns same one)  |
| `Set(value)`     | `Void(Object)`      | Complete with a value and retain runtime-managed values (can only call once) |
| `SetOwned(value)`| `Void(Object)`      | Explicit ownership-retaining alias for `Set`     |
| `SetError(msg)`  | `Void(String)`      | Complete with an error (can only call once)      |

### Properties

| Property | Type                   | Description                            |
|----------|------------------------|----------------------------------------|
| `IsDone` | `Boolean` (read-only)  | True if Set or SetError was called     |

### BASIC Example

```basic
' Create a promise and get its future
DIM promise AS OBJECT = Viper.Threads.Promise.New()
DIM future AS OBJECT = promise.GetFuture()

' Pass future to another thread, keep promise here
' ... later, complete the promise
promise.Set(result)

' Or complete with an error
promise.SetError("Operation failed")

' Or transfer ownership of a runtime-managed object result
promise.SetOwned(someObject)
```

### Notes

- `Set(value)` retains runtime-managed object and string handles until the result is consumed or the Promise/Future pair is finalized.
- `SetOwned(value)` is kept for call sites that want to document ownership explicitly; it has the same retain semantics as `Set`.
- A Promise can only be completed once (either `Set`, `SetOwned`, or `SetError`).

### Errors (Traps)

- `Promise: null object`
- `Promise: already completed` (calling Set or SetError twice)

---

## Viper.Threads.Future

Consumer side of a Future/Promise pair for receiving asynchronous results.

**Type:** Instance class (obtained via `Promise.GetFuture()`)

A Future represents a value that will be available at some point in the future. It is linked to a
Promise which is used to set the value.

### Methods

| Method          | Signature           | Description                                          |
|-----------------|---------------------|------------------------------------------------------|
| `Get()`         | `Object()`          | Block until resolved, return value (traps on error)  |
| `TryGet()`      | `Object()`          | Non-blocking get: returns value if resolved, NULL otherwise |
| `GetFor(ms)`    | `Object(Integer)`   | Timed get: returns value if resolved within `ms` milliseconds, NULL on timeout |
| `Wait()`        | `Void()`            | Block until resolved (value or error)                |
| `WaitFor(ms)`   | `Boolean(Integer)`  | Wait with timeout, returns true if resolved          |

### Properties

| Property  | Type                   | Description                                  |
|-----------|------------------------|----------------------------------------------|
| `IsDone`  | `Boolean` (read-only)  | True if resolved (value or error)            |
| `IsError` | `Boolean` (read-only)  | True if resolved with error                  |
| `Error`   | `String` (read-only)   | Error message (empty if no error)            |

### BASIC Example

```basic
' Get a future from a promise
DIM promise AS OBJECT = Viper.Threads.Promise.New()
DIM future AS OBJECT = promise.GetFuture()

' In producer thread: set the result
promise.Set(result)

' In consumer thread: get the result
DIM value AS PTR = future.Get()
```

### Async Task Example

```basic
SUB ComputeAsync(promise AS OBJECT)
    ' Do some long computation
    DIM result AS INTEGER = ExpensiveComputation()
    promise.Set(result)
END SUB

' Start async computation
DIM promise AS OBJECT = Viper.Threads.Promise.New()
DIM future AS OBJECT = promise.GetFuture()

' Start worker thread
DIM t AS OBJECT = Viper.Threads.Thread.Start(ADDR_OF ComputeAsync, promise)

' Do other work while computation runs
DoOtherWork()

' Wait for result
DIM result AS PTR = future.Get()
t.Join()
```

### Timeout Example

```basic
DIM future AS OBJECT = GetFutureFromSomewhere()

' Wait up to 5 seconds for result
IF future.WaitFor(5000) THEN
    IF NOT future.IsError THEN
        DIM value AS PTR = future.Get()
        PRINT "Got result"
    ELSE
        PRINT "Error: "; future.Error
    END IF
ELSE
    PRINT "Timed out waiting for result"
END IF
```

### Polling Pattern

```basic
DIM future AS OBJECT = GetFutureFromSomewhere()

' Poll without blocking
DO WHILE NOT future.IsDone
    ' Do other work
    ProcessEvents()
    Viper.Time.Clock.Sleep(10)  ' Small delay
LOOP

IF NOT future.IsError THEN
    DIM value AS PTR = future.Get()
END IF
```

### Error Handling

```basic
DIM promise AS OBJECT = Viper.Threads.Promise.New()
DIM future AS OBJECT = promise.GetFuture()

' Producer might fail
IF errorOccurred THEN
    promise.SetError("Failed to compute result")
ELSE
    promise.Set(result)
END IF

' Consumer checks for error
IF future.IsError THEN
    PRINT "Error: "; future.Error
ELSE
    DIM value AS PTR = future.Get()  ' Safe - we know it's not an error
END IF
```

### Errors (Traps)

- `Future: null object`
- `Future: resolved with error` (calling `Get()` on error-resolved future)

### Notes

- A Promise can only be completed once (either `Set` or `SetError`)
- `GetFuture()` returns the cached Future and each call receives its own retained handle.
- Calling `Get()` on an error-resolved Future will trap
- `WaitFor()` returns false on timeout without trapping
- Multiple threads can wait on the same Future
- Completion listeners run outside the Promise lock. If a listener traps, the Future releases all listener references and invokes remaining listeners before rethrowing the first listener trap.
- Cancelling a pending completion listener runs its cleanup hook after removing it. If the cleanup hook traps, the listener and retained Future reference are still released before the trap is rethrown.

---

## Viper.Threads.Parallel

High-level parallel execution utilities for distributing work across CPU cores.

**Type:** Static utility class

Provides common parallel patterns like ForEach, Map, and Invoke using a shared thread pool.

### Methods

| Method                            | Signature                                 | Description                                           |
|-----------------------------------|-------------------------------------------|-------------------------------------------------------|
| `ForEach(seq, func)`              | `Void(Seq, Ptr)`                          | Execute func for each item in parallel                |
| `ForEachPool(seq, func, pool)`    | `Void(Seq, Ptr, Pool)`                    | ForEach with custom thread pool                       |
| `Map(seq, func)`                  | `Seq(Seq, Ptr)`                           | Transform items in parallel, preserve order           |
| `MapPool(seq, func, pool)`        | `Seq(Seq, Ptr, Pool)`                     | Map with custom thread pool                           |
| `Invoke(funcs)`                   | `Void(Seq)`                               | Execute multiple functions in parallel                |
| `InvokePool(funcs, pool)`         | `Void(Seq, Pool)`                         | Invoke with custom thread pool                        |
| `For(start, end, func)`           | `Void(Integer, Integer, Ptr)`             | Parallel for loop over range [start, end)             |
| `ForPool(start, end, func, pool)` | `Void(Integer, Integer, Ptr, Pool)`       | Parallel for with custom pool                         |
| `Reduce(seq, func, identity)`     | `Object(Seq, Ptr, Object)`               | Reduce items in parallel using a binary combine function |
| `ReducePool(seq, func, id, pool)` | `Object(Seq, Ptr, Object, Pool)`          | Reduce with custom thread pool                        |
| `DefaultWorkers()`                | `Integer()`                               | Get number of CPU cores                               |
| `DefaultPool()`                   | `Pool()`                                  | Get or create the shared default thread pool          |

### Zia Example

> Parallel requires function pointers (`addr_of`) which is an advanced Zia feature. Use BASIC `ADDR_OF` for parallel operations.

### BASIC ForEach Example

```basic
SUB ProcessItem(item AS PTR)
    ' Process each item
    PRINT "Processing: "; item
END SUB

' Process all items in parallel
DIM items AS OBJECT = CreateItems()
Viper.Threads.Parallel.ForEach(items, ADDR_OF ProcessItem)
```

### Map Example

```basic
FUNCTION Transform(item AS PTR) AS PTR
    ' Transform the item
    RETURN ComputeResult(item)
END FUNCTION

DIM inputs AS OBJECT = GetInputs()
DIM outputs AS OBJECT = Viper.Threads.Parallel.Map(inputs, ADDR_OF Transform)

' outputs has same length and order as inputs
FOR i = 0 TO outputs.Length - 1
    PRINT "Result "; i; ": "; outputs.Get(i)
NEXT i
```

### Parallel For Example

```basic
SUB ProcessIndex(i AS INTEGER)
    ' Process item at index i
    DoWork(i)
END SUB

' Process indices 0..99 in parallel
Viper.Threads.Parallel.For(0, 100, ADDR_OF ProcessIndex)
```

### Invoke Example

```basic
SUB TaskA()
    PRINT "Task A running"
END SUB

SUB TaskB()
    PRINT "Task B running"
END SUB

SUB TaskC()
    PRINT "Task C running"
END SUB

' Run all three tasks concurrently
DIM tasks AS OBJECT = Viper.Collections.Seq.New()
tasks.Push(ADDR_OF TaskA)
tasks.Push(ADDR_OF TaskB)
tasks.Push(ADDR_OF TaskC)

Viper.Threads.Parallel.Invoke(tasks)
' All tasks completed when Invoke returns
```

### Custom Thread Pool

```basic
' Create a pool with 4 workers
DIM pool AS OBJECT = Viper.Threads.Pool.New(4)

' Use custom pool for parallel operations
Viper.Threads.Parallel.ForEachPool(items, ADDR_OF Process, pool)

' Shut down pool when done
pool.Shutdown()
```

### Notes

- **Default pool:** Operations without explicit pool use a shared pool with `DefaultWorkers()` threads
- **Default pool handle:** `DefaultPool()` returns a retained handle; release it like other runtime objects when using the C ABI directly.
- **Order preservation:** `Map` guarantees output order matches input order
- **Map result ownership:** Runtime-managed objects returned by a `Map` callback are transferred to the result Seq, which releases them when the Seq is freed. A mapper that returns an existing object should retain or otherwise own the returned reference.
- **Blocking:** All parallel operations block until work is complete
- **Thread safety:** Functions passed to parallel operations must be thread-safe
- **Work distribution:** Work is distributed in small chunks for load balancing
- **Reduce identity:** `Reduce` applies the identity value once on the calling thread after per-chunk reduction; workers do not share or mutate the identity concurrently
- **For range bounds:** `For(start, end, func)` traps if the half-open range is larger than `Integer` can represent.
- **Task traps:** If a worker callback traps, the operation wakes its caller and then traps with a `Parallel.*: task trapped` message instead of hanging.
- **VM callback limit:** `Parallel` callback APIs require native callback pointers; VM code should use VM-aware `Thread.Start` or `Async.Run` until VM-backed `Parallel` callbacks are implemented.

### Use Cases

- **Data processing:** Transform large datasets in parallel
- **Batch operations:** Process many files or network requests concurrently
- **Computation:** Parallelize CPU-intensive algorithms
- **Initialization:** Load multiple resources simultaneously

---

## Viper.Threads.CancelToken

Cooperative cancellation token for signaling cancellation to long-running or asynchronous operations. Thread-safe via atomic operations.

**Type:** Instance class
**Constructor:** `Viper.Threads.CancelToken.New()`

### Properties

| Property      | Type    | Description                                |
|---------------|---------|--------------------------------------------|
| `IsCancelled` | Boolean | True if this token or any linked parent has been cancelled |

### Methods

| Method                    | Signature              | Description                                              |
|---------------------------|------------------------|----------------------------------------------------------|
| `Cancel()`                | `Void()`               | Request cancellation on this token                        |
| `Reset()`                 | `Void()`               | Reset the token for reuse                                |
| `Linked()`                | `CancelToken()`  | Create a child token that cancels when parent cancels    |
| `Check()`                 | `Boolean()`            | Check if this or parent token is cancelled               |
| `ThrowIfCancelled()`      | `Void()`               | Trap if the token has been cancelled                     |

### Notes

- **Thread-safe:** All operations use atomic memory operations, safe to call from any thread.
- **Reusable:** `Reset()` clears this token's local cancelled bit so it can be reused.
- **Linked tokens:** Child tokens created with `Linked()` are cancelled when the parent is cancelled. Parent cancellation is propagated into children and is sticky: resetting the parent later does not clear already-cancelled children.
- **Child reset:** After a parent has been reset, a linked child may be reset independently to clear its propagated cancellation state. If the parent is still cancelled, the child continues to report cancelled.
- **Cooperative:** Cancellation is advisory. The operation must check the token and respond appropriately.

### BASIC Example

```basic
' Create a cancellation token
DIM token AS OBJECT = Viper.Threads.CancelToken.New()

' Pass to a long-running operation
SUB ProcessItems(items AS OBJECT, cancel AS OBJECT)
    FOR i = 0 TO items.Length - 1
        ' Check for cancellation periodically
        IF cancel.IsCancelled THEN
            PRINT "Operation cancelled at item "; i
            EXIT SUB
        END IF
        ProcessItem(items.Get(i))
    NEXT
END SUB

' Cancel from another thread or after a condition
token.Cancel()

' Linked tokens for hierarchical cancellation
DIM parentToken AS OBJECT = Viper.Threads.CancelToken.New()
DIM childToken AS OBJECT = parentToken.Linked()

parentToken.Cancel()
PRINT childToken.IsCancelled  ' Output: 1 (true - parent was cancelled)
PRINT childToken.Check()      ' Output: 1

' ThrowIfCancelled traps if cancelled
token.ThrowIfCancelled()  ' Traps: "operation cancelled"
```

### Use Cases

- **Async operations:** Cancel HTTP requests, database queries, or file operations
- **Thread management:** Signal worker threads to stop gracefully
- **Timeout patterns:** Cancel operations that exceed time limits
- **UI responsiveness:** Cancel background work when user navigates away

---

## Viper.Threads.Debouncer

Time-based debouncer that delays execution until a quiet period has elapsed. Useful for coalescing rapid events (e.g., keyboard input, resize events) into a single action.

**Type:** Instance class
**Constructor:** `Viper.Threads.Debouncer.New(delayMs)`

### Properties

| Property      | Type                   | Description                                      |
|---------------|------------------------|--------------------------------------------------|
| `Delay`       | `Integer` (read-only)  | Configured delay in milliseconds                 |
| `IsReady`     | `Boolean` (read-only)  | True if delay has elapsed since last signal      |
| `SignalCount` | `Integer` (read-only)  | Number of signals since last ready state         |

### Methods

| Method       | Signature       | Description                                              |
|--------------|-----------------|----------------------------------------------------------|
| `Signal()`   | `Void()`        | Signal the debouncer (resets the timer)                  |
| `Reset()`    | `Void()`        | Reset debouncer to initial state                         |

### How It Works

1. Call `Signal()` each time an event occurs
2. The timer resets on each signal
3. `IsReady` returns true only after the full delay has elapsed with no new signals
4. This ensures the action fires only after events stop arriving

### BASIC Example

```basic
' Create a debouncer with 300ms delay
DIM debounce AS OBJECT = Viper.Threads.Debouncer.New(300)

' In an event loop (e.g., processing keystrokes)
SUB OnKeystroke(key AS STRING)
    debounce.Signal()
    ' Don't search yet - wait for user to stop typing
END SUB

' In the main loop
IF debounce.get_IsReady() AND debounce.get_SignalCount() > 0 THEN
    ' User stopped typing for 300ms - perform search
    PerformSearch()
    debounce.Reset()
END IF
```

### Use Cases

- **Search-as-you-type:** Wait for user to stop typing before querying
- **Window resize:** Recalculate layout after resizing stops
- **Auto-save:** Save document after editing pauses
- **Network requests:** Coalesce rapid updates into a single API call
- **Thread-safe:** Debouncer state is synchronized internally; multiple threads can call `Signal`, `Reset`, and properties safely.

---

## Viper.Threads.Throttler

Time-based throttler that limits operations to at most once per interval. Unlike debouncing which waits for a quiet period, throttling ensures a minimum time between executions.

**Type:** Instance class
**Constructor:** `Viper.Threads.Throttler.New(intervalMs)`

### Properties

| Property       | Type                   | Description                                                |
|----------------|------------------------|------------------------------------------------------------|
| `CanProceed`   | `Boolean` (read-only)  | True if an operation would be allowed (without marking)    |
| `Count`        | `Integer` (read-only)  | Number of operations allowed so far                        |
| `Interval`     | `Integer` (read-only)  | Configured interval in milliseconds                        |
| `RemainingMs`  | `Integer` (read-only)  | Milliseconds until next operation is allowed (0 if ready)  |

### Methods

| Method         | Signature    | Description                                              |
|----------------|--------------|----------------------------------------------------------|
| `Try()`        | `Boolean()`  | Try to execute (returns true if allowed, marks as used)  |
| `Reset()`      | `Void()`     | Reset throttler to allow immediate operation             |

### How It Works

1. Call `Try()` before performing the rate-limited operation
2. Returns `true` if enough time has passed since the last allowed operation
3. Returns `false` if the interval hasn't elapsed yet
4. The first call always succeeds

### BASIC Example

```basic
' Create a throttler allowing one operation per second
DIM throttle AS OBJECT = Viper.Threads.Throttler.New(1000)

' In an event loop
SUB OnMouseMove(x AS INTEGER, y AS INTEGER)
    ' Only update at most once per second
    IF throttle.Try() THEN
        UpdateDisplay(x, y)
    END IF
END SUB

' Check without consuming
IF throttle.get_CanProceed() THEN
    PRINT "Ready for next operation"
END IF

PRINT "Operations performed: "; throttle.get_Count()
PRINT "Time until next allowed: "; throttle.get_RemainingMs(); "ms"
```

### Debouncer vs Throttler

| Feature          | Debouncer                    | Throttler                      |
|------------------|------------------------------|--------------------------------|
| Timing           | After quiet period           | At regular intervals           |
| First event      | Delayed                      | Immediate                      |
| Rapid events     | Only last event fires        | First event fires, rest skip   |
| Use case         | Wait for user to stop        | Limit rate of execution        |

### Use Cases

- **API rate limiting:** Limit outbound API calls per second
- **UI updates:** Throttle expensive re-renders
- **Logging:** Limit log output rate
- **Polling:** Control polling frequency
- **Thread-safe:** Throttler state is synchronized internally; multiple threads can call `Try`, `Reset`, and properties safely.

---

## Viper.Threads.Scheduler

Named task scheduler for scheduling delayed operations. Tasks are identified by name and become due after a specified delay. Poll-based (not thread-based).

**Type:** Instance class
**Constructor:** `Viper.Threads.Scheduler.New()`

### Properties

| Property  | Type    | Description                           |
|-----------|---------|---------------------------------------|
| `Pending` | Integer | Number of scheduled tasks (due + not-yet-due) |

### Methods

| Method                     | Signature                   | Description                                      |
|----------------------------|-----------------------------|--------------------------------------------------|
| `Schedule(name, delayMs)`  | `Void(String, Integer)`     | Schedule a named task with delay in milliseconds |
| `Cancel(name)`             | `Boolean(String)`           | Cancel a scheduled task by name                  |
| `IsDue(name)`              | `Boolean(String)`           | Check if a named task is due                     |
| `Poll()`                   | `Seq()`                     | Get all due tasks (removes them from scheduler)  |
| `Clear()`                  | `Void()`                    | Remove all scheduled tasks                       |

### Notes

- **Poll-based:** Tasks don't execute automatically. Call `Poll()` or `IsDue()` to check for due tasks.
- **Named tasks:** Tasks are identified by name. Scheduling a task with the same name as an existing task replaces it.
- **Full string keys:** Task names compare by byte length and contents, so strings with embedded NUL bytes remain distinct.
- **Poll ownership:** `Poll()` returns a Seq that owns the returned task-name strings.
- **Monotonic clock:** Uses monotonic clock for accurate timing unaffected by system clock changes.
- **Immediate tasks:** A delay of 0 schedules a task that is immediately due on the next `Poll()`.
- **Thread-safe:** Scheduler operations are internally synchronized; multiple threads may schedule, cancel, poll, and clear the same instance.

### BASIC Example

```basic
' Create a scheduler
DIM sched AS OBJECT = Viper.Threads.Scheduler.New()

' Schedule tasks with different delays
sched.Schedule("save", 5000)      ' Save in 5 seconds
sched.Schedule("refresh", 1000)   ' Refresh in 1 second
sched.Schedule("cleanup", 30000)  ' Cleanup in 30 seconds

PRINT sched.Pending  ' Output: 3

' Check specific task
IF sched.IsDue("refresh") THEN
    DoRefresh()
END IF

' Cancel a task
sched.Cancel("cleanup")
PRINT sched.Pending  ' Output: 2

' Poll for all due tasks in main loop
DO
    DIM due AS OBJECT = sched.Poll()
    FOR i = 0 TO due.Length - 1
        DIM taskName AS STRING = due.Get(i)
        SELECT CASE taskName
            CASE "save": DoSave()
            CASE "refresh": DoRefresh()
        END SELECT
    NEXT

    Viper.Time.Clock.Sleep(100)  ' Poll every 100ms
LOOP WHILE sched.Pending > 0
```

### Use Cases

- **Delayed operations:** Schedule saves, cleanups, or refreshes
- **Game timers:** Schedule game events (spawn, power-up expiry)
- **Retry scheduling:** Schedule retry attempts with delays
- **Batch processing:** Accumulate work and process after delay

---

## Viper.Threads.Async

Async task combinators for composing asynchronous results. Built on Future/Promise.

**Type:** Static utility class

### Methods

| Method                            | Signature                          | Description                                         |
|-----------------------------------|------------------------------------|-----------------------------------------------------|
| `Delay(ms)`                       | `Future(Integer)`                  | Return a Future that resolves after `ms` milliseconds with NULL |
| `All(futures)`                    | `Future(Seq)`                      | Return a Future that resolves when all input futures resolve (with a Seq of results) |
| `Any(futures)`                    | `Future(Object)`                   | Return a Future that resolves with the value of whichever input future completes first |
| `Run(callback, arg)`              | `Future(Ptr, Ptr)`                 | Spawn a thread to run `callback(arg)`, return Future with result |
| `RunOwned(callback, arg)`         | `Future(Ptr, Object)`              | `Run` variant that retains a runtime-managed argument while the callback runs |
| `Map(future, mapper, arg)`        | `Future(Future, Ptr, Ptr)`         | Chain a transformation on a Future's result          |
| `MapOwned(future, mapper, arg)`   | `Future(Future, Ptr, Object)`      | `Map` variant that retains a runtime-managed mapper argument |
| `RunCancellable(callback, arg, token)` | `Future(Ptr, Ptr, CancelToken)` | Like `Run` but linked to a cancellation token      |
| `RunCancellableOwned(callback, arg, token)` | `Future(Ptr, Object, CancelToken)` | `RunCancellable` variant that retains a runtime-managed argument |

### Notes

- All methods are thread-safe.
- `Delay` returns a Future that resolves with NULL after the specified delay.
- `All` returns a Future resolving to a Seq of results. If any input future has an error, the combined Future resolves with that error without waiting for the remaining inputs.
- The Seq returned by `All` owns retained runtime-managed result values, so results remain valid after the source futures are released.
- `Any` returns a Future resolving with the value of the first completed input future.
- `Run` spawns a new thread to execute the callback and returns a Future that resolves with the callback's return value.
- `Map` chains a transformation: when the input future resolves, `mapper` is called with the result and `arg`, producing a new Future.
- `RunCancellable` is like `Run` but associates the spawned task with a `CancelToken` for cooperative cancellation.
- Traps raised inside `Run`, `RunCancellable`, or `Map` callbacks are converted into Future errors.
- Callback `arg` values are forwarded as raw pointers; if you pass non-global native memory, keep it alive until the callback has run.
- Use `RunOwned`, `MapOwned`, and `RunCancellableOwned` when the callback argument is a runtime-managed object or string handle that should be retained for the duration of the callback.
- VM-backed `Async.Run` retains its managed argument for the worker lifetime; native callback use should still choose the `Owned` variants when passing runtime-managed callback arguments.
- Callback-created return values from `Run`, `RunCancellable`, and `Map` are owned by the returned Future. If a callback returns the exact borrowed argument or input Future value, ownership stays borrowed; owned arguments and owned input Future values are retained or transferred safely.
- If a callback wants to transfer ownership of a runtime-managed result explicitly in custom promise/future flows, use `Promise.SetOwned` or the internal transferred-result API.

---

## Viper.Threads.ConcurrentMap

Thread-safe string-keyed hash map for concurrent access from multiple threads.

**Type:** Instance class
**Constructor:** `Viper.Threads.ConcurrentMap.New()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `Length`     | Integer | Number of key-value pairs in the map   |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method                       | Signature                     | Description                                                |
|------------------------------|-------------------------------|------------------------------------------------------------|
| `Set(key, value)`            | `Void(String, Object)`        | Thread-safe insert or update                               |
| `Get(key)`                   | `Object(String)`              | Thread-safe lookup; returns NULL if not found               |
| `GetOr(key, default)`        | `Object(String, Object)`      | Thread-safe lookup; returns `default` if not found          |
| `Has(key)`                   | `Boolean(String)`             | Thread-safe existence check                                |
| `SetIfMissing(key, value)`   | `Boolean(String, Object)`     | Insert only if key absent; returns true if inserted         |
| `Remove(key)`                | `Boolean(String)`             | Thread-safe removal; returns true if found                  |
| `Clear()`                    | `Void()`                      | Thread-safe removal of all entries                          |
| `Keys()`                     | `Seq()`                       | Get snapshot of all keys (may not reflect concurrent writes)|
| `Values()`                   | `Seq()`                       | Get snapshot of all values (may not reflect concurrent writes)|

### Notes

- Uses mutex protection; safe for concurrent reads and writes from any thread.
- Keys are copied on insert (not retained by reference).
- Keys compare by byte length and contents, so strings with embedded NUL bytes are supported.
- Values are retained while in the map.
- `Get`, found-value `GetOr`, and `Values` return stable retained references/snapshots that remain valid even if the map is concurrently updated after the call returns. At the C ABI layer, callers release those returned references.
- Uses FNV-1a hash with separate chaining for collision resolution.
- For single-threaded use, prefer `Viper.Collections.Map` which has no locking overhead.

### Zia Example

```rust
module ConcMapDemo;

bind Viper.Terminal;
bind Viper.Threads.ConcurrentMap as CMap;
bind Viper.Core.Box as Box;
bind Viper.Fmt as Fmt;

func start() {
    var m = CMap.New();
    Say("Empty: " + Fmt.Bool(m.get_IsEmpty()));

    // Set key-value pairs (values must be objects, use Box)
    m.Set("name", Box.Str("Alice"));
    m.Set("age", Box.I64(30));
    Say("Len: " + Fmt.Int(m.get_Length()));

    // Get values
    Say("name: " + Box.ToStr(m.Get("name")));
    Say("age: " + Fmt.Int(Box.ToI64(m.Get("age"))));

    // Has check
    Say("Has name: " + Fmt.Bool(m.Has("name")));

    // SetIfMissing (returns false if key exists)
    Say("SetIfMissing name: " + Fmt.Bool(m.SetIfMissing("name", Box.Str("Bob"))));

    // Remove
    Say("Remove age: " + Fmt.Bool(m.Remove("age")));

    // Clear
    m.Clear();
    Say("Len after Clear: " + Fmt.Int(m.get_Length()));
}
```

### BASIC Example

```basic
DIM m AS OBJECT = Viper.Threads.ConcurrentMap.New()
PRINT "Empty: "; m.IsEmpty

' Set key-value pairs (values are objects, use Box)
m.Set("name", Viper.Core.Box.Str("Alice"))
m.Set("age", Viper.Core.Box.I64(30))
PRINT "Len: "; m.Length

' Get values - use static call form for obj-returning methods
DIM nameVal AS OBJECT = Viper.Threads.ConcurrentMap.Get(m, "name")
PRINT "name: "; Viper.Core.Box.ToStr(nameVal)

DIM ageVal AS OBJECT = Viper.Threads.ConcurrentMap.Get(m, "age")
PRINT "age: "; Viper.Core.Box.ToI64(ageVal)

' Has check
PRINT "Has name: "; m.Has("name")

' SetIfMissing (returns false if key exists)
PRINT "SetIfMissing name: "; m.SetIfMissing("name", Viper.Core.Box.Str("Bob"))

' Remove and Clear
PRINT "Remove age: "; m.Remove("age")
m.Clear()
PRINT "Len after Clear: "; m.Length
```

---

## Viper.Threads.ConcurrentQueue

Thread-safe FIFO queue for concurrent access from multiple threads.

**Type:** Instance class
**Constructor:** `Viper.Threads.ConcurrentQueue.New()`

### Properties

| Property  | Type                   | Description                              |
|-----------|------------------------|------------------------------------------|
| `Length`     | `Integer` (read-only)  | Approximate number of elements           |
| `IsEmpty` | `Boolean` (read-only)  | True if the queue is approximately empty |
| `IsClosed` | `Boolean` (read-only) | True after `Close()` has been called     |

### Methods

| Method               | Signature             | Description                                                      |
|----------------------|-----------------------|------------------------------------------------------------------|
| `Enqueue(item)`      | `Void(Object)`        | Add item to back of queue (thread-safe)                          |
| `Dequeue()`          | `Object()`            | Remove item from front (blocks until available)                  |
| `TryDequeue()`       | `Object()`            | Remove item from front (non-blocking); returns NULL if empty     |
| `DequeueTimeout(ms)` | `Object(Integer)`     | Remove item with timeout; returns NULL if timeout expires        |
| `Peek()`             | `Object()`            | Peek at front item without removing; returns NULL if empty       |
| `Clear()`            | `Void()`              | Remove all items (thread-safe)                                   |
| `Close()`            | `Void()`              | Close the queue, wake blocked dequeuers, and reject future enqueues |

### Notes

- FIFO order is guaranteed.
- `Dequeue` blocks until an item is available or the queue is closed and drained.
- `TryDequeue` returns NULL immediately if the queue is empty.
- `Peek` returns a stable retained reference to the current front item, or NULL if empty.
- `Dequeue`, `TryDequeue`, and `DequeueTimeout` transfer the queue's retained item reference to the caller. At the C ABI layer, callers release returned runtime-managed values.
- `Close()` wakes blocked `Dequeue`/`DequeueTimeout` calls; once the queue is empty they return NULL.
- Values are retained while in the queue.

### Zia Example

```rust
module ConcQueueDemo;

bind Viper.Terminal;
bind Viper.Threads.ConcurrentQueue as CQ;
bind Viper.Core.Box as Box;
bind Viper.Fmt as Fmt;

func start() {
    var q = CQ.New();
    Say("Empty: " + Fmt.Bool(q.get_IsEmpty()));

    // Enqueue items
    q.Enqueue(Box.I64(10));
    q.Enqueue(Box.I64(20));
    q.Enqueue(Box.I64(30));
    Say("Len: " + Fmt.Int(q.get_Length()));

    // Peek (non-destructive)
    Say("Peek: " + Fmt.Int(Box.ToI64(q.Peek())));

    // TryDequeue (non-blocking)
    Say("TryDequeue: " + Fmt.Int(Box.ToI64(q.TryDequeue())));

    // Dequeue (blocking, but items available)
    Say("Dequeue: " + Fmt.Int(Box.ToI64(q.Dequeue())));

    // DequeueTimeout
    Say("DequeueTimeout: " + Fmt.Int(Box.ToI64(q.DequeueTimeout(100))));
    Say("Empty after all: " + Fmt.Bool(q.get_IsEmpty()));

    // Clear
    q.Enqueue(Box.I64(99));
    q.Clear();
    Say("Len after clear: " + Fmt.Int(q.get_Length()));
}
```

### BASIC Example

```basic
DIM q AS OBJECT = Viper.Threads.ConcurrentQueue.New()
PRINT "Empty: "; q.IsEmpty

' Enqueue items (values are objects, use Box)
q.Enqueue(Viper.Core.Box.I64(10))
q.Enqueue(Viper.Core.Box.I64(20))
q.Enqueue(Viper.Core.Box.I64(30))
PRINT "Len: "; q.Length

' Peek (non-destructive)
PRINT "Peek: "; Viper.Core.Box.ToI64(q.Peek())

' TryDequeue (non-blocking)
PRINT "TryDequeue: "; Viper.Core.Box.ToI64(q.TryDequeue())

' Dequeue (blocking, but items available)
PRINT "Dequeue: "; Viper.Core.Box.ToI64(q.Dequeue())

' DequeueTimeout
PRINT "DequeueTimeout: "; Viper.Core.Box.ToI64(q.DequeueTimeout(100))
PRINT "Empty after all: "; q.IsEmpty

' Clear
q.Enqueue(Viper.Core.Box.I64(99))
q.Clear()
PRINT "Len after clear: "; q.Length
```

---

## Viper.Threads.Channel

Thread-safe bounded channel for inter-thread communication. Supports blocking, non-blocking, and timeout-based send/receive operations.

**Type:** Instance class (requires `New(capacity)`)

### Constructor

| Method           | Signature          | Description                                         |
|------------------|--------------------|-----------------------------------------------------|
| `New(capacity)`  | `Channel(Integer)` | Create a bounded channel (0 for synchronous channel)|

### Methods

| Method                  | Signature                    | Description                                                  |
|-------------------------|------------------------------|--------------------------------------------------------------|
| `Send(item)`            | `Void(Object)`               | Send item, blocking if full (traps if closed)                |
| `TrySend(item)`         | `Boolean(Object)`            | Try to send without blocking; returns false if full or closed|
| `SendFor(item, ms)`     | `Boolean(Object, Integer)`   | Send with timeout; returns false if timed out or closed      |
| `Recv()`                | `Object()`                   | Receive item, blocking if empty; returns NULL if closed and empty |
| `TryRecv()`             | `Object()`                   | Try to receive without blocking; returns NULL if empty       |
| `RecvFor(ms)`           | `Object(Integer)`            | Receive with timeout; returns NULL if timed out or closed    |
| `Close()`               | `Void()`                     | Close the channel; wakes all blocked senders/receivers       |

### Properties

| Property   | Type                   | Description                           |
|------------|------------------------|---------------------------------------|
| `Length`      | `Integer` (read-only)  | Number of items currently in channel  |
| `Cap`      | `Integer` (read-only)  | Channel capacity (0 for synchronous)  |
| `IsClosed` | `Boolean` (read-only)  | True if the channel has been closed   |
| `IsEmpty`  | `Boolean` (read-only)  | True if the channel contains no items |
| `IsFull`   | `Boolean` (read-only)  | True if the channel is at capacity    |

### Notes

- Closing a channel prevents further sends but receivers can still drain remaining items.
- A synchronous channel (capacity 0) blocks the sender until a receiver is ready.
- On a synchronous channel, `TryRecv()` is strictly non-blocking. It only consumes an already-published handoff value; it does not wait to rendezvous with a merely waiting sender. Use `Recv()` or `RecvFor()` for rendezvous receives.
- At the C ABI layer, `rt_channel_try_recv(channel, NULL)` checks only an already-queued value without consuming or releasing it; it does not advertise a merely waiting synchronous sender as available.
- `IsFull` means a send would block. For synchronous channels it is false when a receiver is already waiting and no handoff value is queued.
- `SendFor` includes both the wait for a receiver/space and the synchronous handoff acknowledgement in its timeout budget.
- `Send` traps if the channel is closed.

### Zia Example

```rust
module ChannelDemo;

bind Viper.Terminal;
bind Viper.Threads.Channel as Channel;
bind Viper.Core.Box as Box;
bind Viper.Fmt as Fmt;

func start() {
    var ch = Channel.New(8);
    Say("Cap: " + Fmt.Int(ch.get_Cap()));      // 8
    Say("IsEmpty: " + Fmt.Bool(ch.get_IsEmpty()));  // true

    // Send items
    ch.Send(Box.I64(1));
    ch.Send(Box.I64(2));
    ch.Send(Box.I64(3));
    Say("Len: " + Fmt.Int(ch.get_Length()));       // 3

    // Non-blocking send (returns false if full or closed)
    Say("TrySend: " + Fmt.Bool(ch.TrySend(Box.I64(4))));  // true

    // Receive items
    Say("Recv: " + Fmt.Int(Box.ToI64(ch.Recv())));    // 1
    Say("Recv: " + Fmt.Int(Box.ToI64(ch.Recv())));    // 2

    // Close channel
    ch.Close();
    Say("IsClosed: " + Fmt.Bool(ch.get_IsClosed()));  // true
}
```

### BASIC Producer/Consumer Example

```basic
' Bounded channel with capacity 16
DIM ch AS OBJECT = Viper.Threads.Channel.New(16)

' Producer: send items
SUB Producer(channel AS PTR)
    DIM i AS INTEGER
    FOR i = 1 TO 10
        channel.Send(Viper.Core.Box.I64(i))
    NEXT i
    channel.Close()
END SUB

' Consumer: receive until closed and empty
SUB Consumer(channel AS PTR)
    DO
        DIM item AS OBJECT = channel.Recv()
        IF item = NULL THEN EXIT DO   ' closed and drained
        PRINT "Received: "; Viper.Core.Box.ToI64(item)
    LOOP
END SUB

' In a single-threaded context, demonstrate send/recv
DIM i AS INTEGER
FOR i = 1 TO 5
    ch.Send(Viper.Core.Box.I64(i * 10))
NEXT i
PRINT "Len: "; ch.Length     ' Output: 5
PRINT "IsFull: "; ch.IsFull  ' Output: 0 (cap 16, only 5 items)

' Non-blocking receive
DIM item AS OBJECT = ch.Recv()
PRINT "Recv: "; Viper.Core.Box.ToI64(item)  ' Output: 10

' Timeout-based receive (100ms)
DIM timed AS OBJECT = ch.RecvFor(100)
IF timed <> NULL THEN
    PRINT "Got: "; Viper.Core.Box.ToI64(timed)
ELSE
    PRINT "Timed out"
END IF

' Send with timeout
DIM sent AS INTEGER = ch.SendFor(Viper.Core.Box.I64(99), 200)
PRINT "Sent: "; sent

ch.Close()
PRINT "IsClosed: "; ch.IsClosed  ' Output: 1
```

---

## See Also

- [Collections](collections/README.md) - Thread-safe access to shared data structures
- [Time & Timing](time.md) - `Clock.Sleep()` and timing utilities
