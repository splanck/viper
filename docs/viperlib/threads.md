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

---

## Viper.Threads.Thread

OS threads for Viper programs (VM and native backends).

**Type:** Instance class (created by `Start`)

### Methods

| Method                    | Signature                       | Description                                |
|--------------------------|----------------------------------|--------------------------------------------|
| `Start(entry, arg)`      | `Thread(Ptr, Ptr)`               | Start a new thread running `entry(arg)`    |
| `Join()`                 | `Void()`                         | Wait until the thread finishes             |
| `TryJoin()`              | `Boolean()`                      | Non-blocking join attempt                  |
| `JoinFor(ms)`            | `Boolean(Integer)`               | Join with timeout in milliseconds          |
| `Sleep(ms)`              | `Void(Integer)`                  | Sleep the current thread (ms, clamped)     |
| `Yield()`                | `Void()`                         | Yield the current thread’s time slice      |

### Properties

| Property   | Type                  | Description                            |
|------------|-----------------------|----------------------------------------|
| `Id`       | `Integer` (read-only) | Monotonic thread id (1, 2, 3, …)       |
| `IsAlive`  | `Boolean` (read-only) | True while entry function is running   |

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

### Join Timeouts

`JoinFor(ms)` behaves as:

| `ms` value | Behavior                         |
|------------|----------------------------------|
| `< 0`      | Wait indefinitely (same as Join) |
| `= 0`      | Immediate check (same as TryJoin)|
| `> 0`      | Wait up to `ms` milliseconds     |

### Errors (Traps)

- `Thread.Start: null entry`
- `Thread.Start: failed to create thread`
- `Thread.Join: null thread`
- `Thread.Join: already joined`
- `Thread.Join: cannot join self`

---

## Viper.Threads.Monitor

FIFO-fair, re-entrant monitor for explicit object locking.

**Type:** Static utility class

### Methods

| Method                      | Signature                    | Description                                         |
|----------------------------|------------------------------|-----------------------------------------------------|
| `Enter(obj)`               | `Void(Object)`               | Acquire monitor (blocks, FIFO)                      |
| `TryEnter(obj)`            | `Boolean(Object)`            | Try to acquire immediately                          |
| `TryEnterFor(obj, ms)`     | `Boolean(Object, Integer)`   | Try to acquire with timeout (ms)                    |
| `Exit(obj)`                | `Void(Object)`               | Release monitor (balances `Enter`)                  |
| `Wait(obj)`                | `Void(Object)`               | Release monitor and wait for `Pause`/`PauseAll`     |
| `WaitFor(obj, ms)`         | `Boolean(Object, Integer)`   | Wait with timeout (returns false on timeout)        |
| `Pause(obj)`               | `Void(Object)`               | Wake one waiter (FIFO)                              |
| `PauseAll(obj)`            | `Void(Object)`               | Wake all waiters (FIFO order, then FIFO re-acquire) |

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
| `Get()`                              | `Integer()`                            | Read current value                              |
| `Set(value)`                         | `Void(Integer)`                        | Write value                                     |
| `Add(delta)`                         | `Integer(Integer)`                     | Add and return new value                        |
| `CompareExchange(expected, desired)` | `Integer(Integer, Integer)`            | CAS; returns the value read before any update   |

### Notes

- Operations are implemented by acquiring a FIFO monitor on the `SafeI64` object itself.
- You can also lock a `SafeI64` explicitly via `Monitor.Enter(cell)` when you need to group multiple operations.

### Errors (Traps)

- `SafeI64.New: alloc failed`
- `SafeI64.Get: null object`
- `SafeI64.Set: null object`
- `SafeI64.Add: null object`
- `SafeI64.CompareExchange: null object`

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
| `TryEnter()`       | `Boolean()`          | Try to acquire immediately                           |
| `TryEnterFor(ms)`  | `Boolean(Integer)`   | Try to acquire with timeout in milliseconds          |
| `Leave()`          | `Void()`             | Release one permit; wakes one waiter if present      |
| `Leave(count)`     | `Void(Integer)`      | Release `count` permits; wakes up to `count` waiters |

### Properties

| Property    | Type                  | Description                    |
|-------------|-----------------------|--------------------------------|
| `Permits`   | `Integer` (read-only) | Current available permit count |

### Notes

- **FIFO:** Contended `Enter`/`TryEnterFor` acquisition is FIFO-fair.
- **Timeout units:** milliseconds. `TryEnterFor` treats negative values as `0` (immediate).

### Errors (Traps)

- `Gate.New: permits cannot be negative`
- `Gate.Enter: null object`
- `Gate.TryEnter: null object`
- `Gate.TryEnterFor: null object`
- `Gate.Leave: null object`
- `Gate.Leave: count cannot be negative`

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
| `WriteEnter()`     | `Void()`      | Acquire exclusive write lock (blocks on readers/writer) |
| `WriteExit()`      | `Void()`      | Release write lock                                      |
| `TryReadEnter()`   | `Boolean()`   | Non-blocking read acquire                               |
| `TryWriteEnter()`  | `Boolean()`   | Non-blocking write acquire                              |

### Properties

| Property         | Type                   | Description              |
|------------------|------------------------|--------------------------|
| `Readers`        | `Integer` (read-only)  | Count of active readers  |
| `IsWriteLocked`  | `Boolean` (read-only)  | True if write lock held  |

### Notes

- **Writer preference:** new readers block while any writer is waiting.

### Errors (Traps)

- `RwLock.ReadEnter: null object`
- `RwLock.ReadExit: null object`
- `RwLock.ReadExit: exit without matching enter`
- `RwLock.WriteEnter: null object`
- `RwLock.WriteExit: null object`
- `RwLock.WriteExit: exit without matching enter`
- `RwLock.WriteExit: not owner`
