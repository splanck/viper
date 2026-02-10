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
- [Viper.Threads.Promise](#viperthreadspromise)
- [Viper.Threads.Future](#viperthreadsfuture)
- [Viper.Threads.Async](#viperthreadsasync)
- [Viper.Threads.ConcurrentMap](#viperthreadsconcurrentmap)
- [Viper.Threads.Parallel](#viperthreadsparallel)
- [Viper.Threads.CancelToken](#viperthreadscanceltoken)
- [Viper.Threads.Debouncer](#viperthreadsdebouncer)
- [Viper.Threads.Throttler](#viperthreadsthrottler)
- [Viper.Threads.Scheduler](#viperthreadsscheduler)

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

### Zia Example

> Thread requires function pointers (`addr_of`) for entry functions, which is an advanced Zia feature. See the IL example above or use BASIC `ADDR_OF` for thread creation.

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

### Zia Example

> Monitor is a static utility class that operates on any object. Use `bind Viper.Threads.Monitor as Monitor;` and call `Monitor.Enter(obj)` / `Monitor.Exit(obj)`. Typically used with threads for synchronization.

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

### Zia Example

```zia
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

### Zia Example

```zia
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

```zia
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

### Zia Example

```zia
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
| `Set(value)`     | `Void(Ptr)`         | Complete with a value (can only call once)       |
| `SetError(msg)`  | `Void(String)`      | Complete with an error (can only call once)      |

### Properties

| Property | Type                   | Description                            |
|----------|------------------------|----------------------------------------|
| `IsDone` | `Boolean` (read-only)  | True if Set or SetError was called     |

### Zia Example

> Promise is not yet constructible from Zia (`New` symbol not resolved). Use BASIC for Promise/Future patterns.

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
```

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
| `Get()`         | `Ptr()`             | Block until resolved, return value (traps on error)  |
| `GetFor(ms)`    | `Boolean(Integer)`  | Wait with timeout, returns false on timeout/error    |
| `TryGet(out)`   | `Boolean(Ptr)`      | Non-blocking get, returns false if not resolved      |
| `Wait()`        | `Void()`            | Block until resolved (value or error)                |
| `WaitFor(ms)`   | `Boolean(Integer)`  | Wait with timeout, returns true if resolved          |

### Properties

| Property  | Type                   | Description                                  |
|-----------|------------------------|----------------------------------------------|
| `IsDone`  | `Boolean` (read-only)  | True if resolved (value or error)            |
| `IsError` | `Boolean` (read-only)  | True if resolved with error                  |
| `Error`   | `String` (read-only)   | Error message (empty if no error)            |

### Zia Example

> Future is obtained via `Promise.GetFuture()`. Since Promise is not yet constructible from Zia, Future is also not accessible. Use BASIC for async result patterns.

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

### Try-Get Pattern

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
- Calling `Get()` on an error-resolved Future will trap
- `GetFor()` and `TryGet()` return false instead of trapping on error
- Multiple threads can wait on the same Future

---

## Viper.Threads.Parallel

High-level parallel execution utilities for distributing work across CPU cores.

**Type:** Static utility class

Provides common parallel patterns like ForEach, Map, and Invoke using a shared thread pool.

### Methods

| Method                      | Signature                            | Description                                           |
|-----------------------------|--------------------------------------|-------------------------------------------------------|
| `ForEach(seq, func)`        | `Void(Seq, Ptr)`                     | Execute func for each item in parallel                |
| `ForEachPool(seq,func,pool)`| `Void(Seq, Ptr, Pool)`               | ForEach with custom thread pool                       |
| `Map(seq, func)`            | `Seq(Seq, Ptr)`                      | Transform items in parallel, preserve order           |
| `MapPool(seq, func, pool)`  | `Seq(Seq, Ptr, Pool)`                | Map with custom thread pool                           |
| `Invoke(funcs)`             | `Void(Seq)`                          | Execute multiple functions in parallel                |
| `InvokePool(funcs, pool)`   | `Void(Seq, Pool)`                    | Invoke with custom thread pool                        |
| `For(start, end, func)`     | `Void(Integer, Integer, Ptr)`        | Parallel for loop over range [start, end)             |
| `ForPool(start,end,func,p)` | `Void(Integer, Integer, Ptr, Pool)`  | Parallel for with custom pool                         |
| `DefaultWorkers()`          | `Integer()`                          | Get number of CPU cores                               |
| `DefaultPool()`             | `Pool()`                             | Get or create the shared default thread pool          |

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
FOR i = 0 TO outputs.Len - 1
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
- **Order preservation:** `Map` guarantees output order matches input order
- **Blocking:** All parallel operations block until work is complete
- **Thread safety:** Functions passed to parallel operations must be thread-safe
- **Work distribution:** Work is distributed in small chunks for load balancing

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
| `IsCancelled` | Boolean | True if cancellation has been requested    |

### Methods

| Method                    | Signature              | Description                                              |
|---------------------------|------------------------|----------------------------------------------------------|
| `Cancel()`                | `Void()`               | Request cancellation (irreversible once set)              |
| `Reset()`                 | `Void()`               | Reset the token for reuse                                |
| `Linked()`                | `CancelToken()`  | Create a child token that cancels when parent cancels    |
| `Check()`                 | `Boolean()`            | Check if this or parent token is cancelled               |
| `ThrowIfCancelled()`      | `Void()`               | Trap if the token has been cancelled                     |

### Notes

- **Thread-safe:** All operations use atomic memory operations, safe to call from any thread.
- **One-way:** Once `Cancel()` is called, `IsCancelled` returns true permanently (until `Reset()`).
- **Linked tokens:** Child tokens created with `Linked()` are automatically cancelled when the parent is cancelled.
- **Cooperative:** Cancellation is advisory. The operation must check the token and respond appropriately.

### Zia Example

> CancelToken is not yet constructible from Zia (`New` symbol not resolved). Use BASIC for cancellation patterns.

### BASIC Example

```basic
' Create a cancellation token
DIM token AS OBJECT = Viper.Threads.CancelToken.New()

' Pass to a long-running operation
SUB ProcessItems(items AS OBJECT, cancel AS OBJECT)
    FOR i = 0 TO items.Len - 1
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
PRINT childToken.Check()  ' Output: 1 (true - parent was cancelled)

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

| Property      | Type    | Description                                      |
|---------------|---------|--------------------------------------------------|
| `Delay`       | Integer | Configured delay in milliseconds                 |
| `SignalCount` | Integer | Number of signals since last ready state         |

### Methods

| Method       | Signature       | Description                                              |
|--------------|-----------------|----------------------------------------------------------|
| `Signal()`   | `Void()`        | Signal the debouncer (resets the timer)                  |
| `IsReady()`  | `Boolean()`     | Check if delay has elapsed since last signal             |
| `Reset()`    | `Void()`        | Reset debouncer to initial state                         |

### How It Works

1. Call `Signal()` each time an event occurs
2. The timer resets on each signal
3. `IsReady()` returns true only after the full delay has elapsed with no new signals
4. This ensures the action fires only after events stop arriving

### Zia Example

> Debouncer is not yet constructible from Zia (`New` symbol not resolved). Use BASIC for debouncing patterns.

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
IF debounce.IsReady() AND debounce.SignalCount > 0 THEN
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

---

## Viper.Threads.Throttler

Time-based throttler that limits operations to at most once per interval. Unlike debouncing which waits for a quiet period, throttling ensures a minimum time between executions.

**Type:** Instance class
**Constructor:** `Viper.Threads.Throttler.New(intervalMs)`

### Properties

| Property       | Type    | Description                                         |
|----------------|---------|-----------------------------------------------------|
| `Interval`     | Integer | Configured interval in milliseconds                 |
| `Count`        | Integer | Number of operations allowed so far                 |
| `RemainingMs`  | Integer | Milliseconds until next operation is allowed (0 if ready) |

### Methods

| Method         | Signature    | Description                                              |
|----------------|--------------|----------------------------------------------------------|
| `Try()`        | `Boolean()`  | Try to execute (returns true if allowed, marks as used)  |
| `CanProceed()` | `Boolean()`  | Check if an operation would be allowed (without marking) |
| `Reset()`      | `Void()`     | Reset throttler to allow immediate operation             |

### How It Works

1. Call `Try()` before performing the rate-limited operation
2. Returns `true` if enough time has passed since the last allowed operation
3. Returns `false` if the interval hasn't elapsed yet
4. The first call always succeeds

### Zia Example

> Throttler is not yet constructible from Zia (`New` symbol not resolved). Use BASIC for throttling patterns.

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
IF throttle.CanProceed() THEN
    PRINT "Ready for next operation"
END IF

PRINT "Operations performed: "; throttle.Count
PRINT "Time until next allowed: "; throttle.RemainingMs; "ms"
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
- **Monotonic clock:** Uses monotonic clock for accurate timing unaffected by system clock changes.
- **Immediate tasks:** A delay of 0 schedules a task that is immediately due on the next `Poll()`.

### Zia Example

> Scheduler is not yet constructible from Zia (`New` symbol not resolved). Use BASIC for task scheduling.

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
    FOR i = 0 TO due.Len - 1
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

Async task combinators for running operations on background threads and composing their results. Built on
Future/Promise and the thread pool.

**Type:** Static utility class

### Methods

| Method                     | Signature                          | Description                                         |
|----------------------------|------------------------------------|-----------------------------------------------------|
| `Run(callback, arg)`       | `Future(Ptr, Ptr)`                 | Run a callback on a background thread               |
| `WaitAll(futures)`         | `Void(Seq)`                        | Block until all futures in the Seq complete          |
| `WaitAny(futures)`         | `Integer(Seq)`                     | Block until any future completes; returns its index  |
| `Map(future, fn, arg)`     | `Future(Future, Ptr, Ptr)`         | Apply a transformation when a future completes       |

### Notes

- All methods are thread-safe.
- `Run` spawns work on a background thread and returns immediately with a Future.
- `WaitAll` blocks the calling thread until every future in the Seq has resolved.
- `Map` chains a transformation: when the input future resolves, `fn` is called with the result.

---

## Viper.Threads.ConcurrentMap

Thread-safe string-keyed hash map for concurrent access from multiple threads.

**Type:** Instance class
**Constructor:** `Viper.Threads.ConcurrentMap.New()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `Len`     | Integer | Number of key-value pairs in the map   |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method            | Signature              | Description                                                |
|-------------------|------------------------|------------------------------------------------------------|
| `Set(key, value)` | `Void(String, Object)` | Thread-safe insert or update                               |
| `Get(key)`        | `Object(String)`       | Thread-safe lookup; returns NULL if not found               |
| `Has(key)`        | `Boolean(String)`      | Thread-safe existence check                                |
| `Remove(key)`     | `Boolean(String)`      | Thread-safe removal; returns true if found                  |
| `Clear()`         | `Void()`               | Thread-safe removal of all entries                          |
| `Keys()`          | `Seq()`                | Get snapshot of all keys (may not reflect concurrent writes)|

### Notes

- Uses mutex protection; safe for concurrent reads and writes from any thread.
- Keys are copied on insert (not retained by reference).
- Values are retained (reference count incremented) while in the map.
- Uses FNV-1a hash with separate chaining for collision resolution.
- For single-threaded use, prefer `Viper.Collections.Map` which has no locking overhead.

---

## See Also

- [Collections](collections.md) - Thread-safe access to shared data structures
- [Time & Timing](time.md) - `Clock.Sleep()` and timing utilities
