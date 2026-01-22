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
- [Viper.Threads.Channel](#viperthreadschannel)

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

---

## Viper.Threads.Pool

Thread pool for executing tasks concurrently with a fixed number of worker threads.

**Type:** Instance class (requires `New(size)`)

### Constructor

| Method       | Signature      | Description                                |
|--------------|----------------|--------------------------------------------|
| `New(size)`  | `Pool(Integer)`| Create a pool with `size` worker threads   |

### Methods

| Method               | Signature                    | Description                                        |
|----------------------|------------------------------|----------------------------------------------------|
| `Submit(fn, arg)`    | `Boolean(Ptr, Ptr)`          | Submit a task; returns true if accepted            |
| `Wait()`             | `Void()`                     | Wait for all pending tasks to complete             |
| `WaitFor(ms)`        | `Boolean(Integer)`           | Wait with timeout; returns true if all completed   |
| `Shutdown()`         | `Void()`                     | Stop accepting tasks, finish queued tasks          |
| `ShutdownNow()`      | `Void()`                     | Stop immediately, discard queued tasks             |

### Properties

| Property     | Type                   | Description                           |
|--------------|------------------------|---------------------------------------|
| `Size`       | `Integer` (read-only)  | Number of worker threads              |
| `Pending`    | `Integer` (read-only)  | Number of tasks waiting to execute    |
| `Active`     | `Integer` (read-only)  | Number of tasks currently executing   |
| `IsShutdown` | `Boolean` (read-only)  | True if pool has been shut down       |

### Task Function

`Submit(fn, arg)` expects `fn` to be a function pointer with signature:

- `void(ptr)` — Function receives `arg` as its parameter

### Notes

- Workers execute tasks in FIFO order
- `Submit` returns false if the pool is shut down
- `Shutdown` blocks until all queued tasks complete
- `ShutdownNow` interrupts waiting tasks (running tasks finish)
- After shutdown, `Submit` always returns false

### Example

```basic
' Create a thread pool with 4 workers
DIM pool AS OBJECT = Viper.Threads.Pool.New(4)
PRINT "Pool size: "; pool.Size

' Define a worker function
FUNCTION DoWork(arg AS Ptr)
    DIM id AS Integer = PTR_TO_INT(arg)
    PRINT "Processing task "; id
    Viper.Threads.Thread.Sleep(100)  ' Simulate work
END FUNCTION

' Submit tasks
FOR i = 1 TO 20
    pool.Submit(ADDR(DoWork), INT_TO_PTR(i))
NEXT i

PRINT "Pending: "; pool.Pending
PRINT "Active: "; pool.Active

' Wait for completion with timeout
IF pool.WaitFor(5000) THEN
    PRINT "All tasks completed"
ELSE
    PRINT "Timeout - some tasks still running"
END IF

' Clean shutdown
pool.Shutdown()
PRINT "Pool shut down: "; pool.IsShutdown
```

### Errors (Traps)

- `Pool.New: size must be >= 1`
- `Pool.Submit: null pool`
- `Pool.Submit: null function`
- `Pool.Wait: null pool`
- `Pool.Shutdown: null pool`

### Use Cases

- **Parallel processing:** Distribute work across CPU cores
- **Batch operations:** Process multiple files or records concurrently
- **Web servers:** Handle requests with limited thread resources
- **Background tasks:** Offload work from main thread

---

## Viper.Threads.Channel

Typed, bounded channel for thread-safe message passing between threads. Channels support both blocking and non-blocking
send/receive operations with optional timeouts.

**Type:** Instance class (requires `New(capacity)`)

### Constructor

| Method           | Signature          | Description                                |
|------------------|--------------------|--------------------------------------------|
| `New(capacity)`  | `Channel(Integer)` | Create a channel with given buffer capacity|

### Send Methods

| Method               | Signature                    | Description                                         |
|----------------------|------------------------------|-----------------------------------------------------|
| `Send(value)`        | `Void(Object)`               | Send value; blocks if channel is full               |
| `TrySend(value)`     | `Boolean(Object)`            | Try to send immediately; returns false if full      |
| `SendFor(value, ms)` | `Boolean(Object, Integer)`   | Send with timeout; returns false on timeout         |

### Receive Methods

| Method               | Signature                    | Description                                         |
|----------------------|------------------------------|-----------------------------------------------------|
| `Recv()`             | `Object()`                   | Receive value; blocks if channel is empty           |
| `TryRecv(out)`       | `Boolean(Ptr)`               | Try to receive immediately; stores in out, false if empty |
| `RecvFor(out, ms)`   | `Boolean(Ptr, Integer)`      | Receive with timeout; stores in out, false on timeout |

### Control Methods

| Method    | Signature | Description                                      |
|-----------|-----------|--------------------------------------------------|
| `Close()` | `Void()`  | Close the channel (no more sends allowed)        |

### Properties

| Property   | Type                   | Description                                   |
|------------|------------------------|-----------------------------------------------|
| `Len`      | `Integer` (read-only)  | Number of items currently in channel          |
| `Cap`      | `Integer` (read-only)  | Channel capacity (buffer size)                |
| `IsClosed` | `Boolean` (read-only)  | True if channel has been closed               |
| `IsEmpty`  | `Boolean` (read-only)  | True if channel has no items                  |
| `IsFull`   | `Boolean` (read-only)  | True if channel is at capacity                |

### Notes

- **Capacity 0:** Unbuffered channel — send blocks until receiver is ready
- **Capacity > 0:** Buffered channel — send blocks only when buffer is full
- **Closed channel:** `Send` traps; `Recv` returns remaining items then returns null
- **Multiple producers/consumers:** Channels are thread-safe for concurrent access
- Items are delivered in FIFO order

### Example

```basic
' Create a buffered channel with capacity 10
DIM ch AS OBJECT = Viper.Threads.Channel.New(10)
PRINT "Capacity: "; ch.Cap

' Producer thread
FUNCTION Producer(ch AS Object)
    FOR i = 1 TO 100
        DIM msg AS OBJECT = Viper.Box.I64(i)
        ch.Send(msg)
        PRINT "Sent: "; i
    NEXT i
    ch.Close()
END FUNCTION

' Consumer thread
FUNCTION Consumer(ch AS Object)
    DO WHILE NOT ch.IsClosed OR NOT ch.IsEmpty
        DIM result AS OBJECT
        IF ch.TryRecv(ADDR(result)) THEN
            DIM value AS Integer = Viper.Unbox.I64(result)
            PRINT "Received: "; value
        ELSE
            Viper.Threads.Thread.Yield()
        END IF
    LOOP
END FUNCTION

' Start threads
DIM producer AS OBJECT = Viper.Threads.Thread.Start(ADDR(Producer), ch)
DIM consumer AS OBJECT = Viper.Threads.Thread.Start(ADDR(Consumer), ch)

' Wait for completion
producer.Join()
consumer.Join()
```

### Non-blocking Example

```basic
DIM ch AS OBJECT = Viper.Threads.Channel.New(5)

' Non-blocking send
IF ch.TrySend(Viper.Box.Str("message")) THEN
    PRINT "Message sent"
ELSE
    PRINT "Channel full"
END IF

' Send with timeout (1 second)
IF ch.SendFor(Viper.Box.Str("timeout test"), 1000) THEN
    PRINT "Sent within timeout"
ELSE
    PRINT "Send timed out"
END IF

' Non-blocking receive
DIM result AS OBJECT
IF ch.TryRecv(ADDR(result)) THEN
    PRINT "Received: "; Viper.Unbox.Str(result)
ELSE
    PRINT "Channel empty"
END IF
```

### Errors (Traps)

- `Channel.New: capacity cannot be negative`
- `Channel.Send: null channel`
- `Channel.Send: channel is closed`
- `Channel.Recv: null channel`
- `Channel.Close: null channel`
- `Channel.Close: already closed`

### Use Cases

- **Producer-consumer:** Decouple data production from consumption
- **Pipeline processing:** Chain processing stages with channels
- **Fan-out/fan-in:** Distribute work to multiple workers, collect results
- **Event queues:** Thread-safe event delivery
- **Rate limiting:** Control flow between threads

---

## See Also

- [Collections](collections.md) - Thread-safe access to shared data structures
- [Time & Timing](time.md) - `Clock.Sleep()` and timing utilities
