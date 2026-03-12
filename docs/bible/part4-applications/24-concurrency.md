# Chapter 24: Concurrency

You tap an app on your phone. You expect it to respond instantly. But what if the app is downloading a file? Or saving to the cloud? Or processing a photo?

If a program can only do one thing at a time, everything waits. You tap a button, and the app freezes while it fetches data from a server. You resize an image, and the entire interface locks up. The screen goes white. The dreaded "Not Responding" appears.

This is not acceptable. Modern users expect applications that stay responsive, that handle multiple tasks smoothly, that take advantage of all those CPU cores sitting inside their devices.

This chapter teaches you to write programs that do many things at once. This is *concurrency* — one of the most powerful and challenging topics in programming.

---

## Why Concurrency Matters

Consider a program that downloads 100 images:

```rust
// Sequential: ~100 seconds (1 second per image)
bind Http = Viper.Network.Http;

for url in urls {
    var image = Http.Get(url);
    images.Push(image);
}
```

Most of that time is spent *waiting*. Your CPU, capable of billions of operations per second, sits idle while the network responds. Now imagine running downloads in parallel — same work, 50-100x faster.

Concurrency lets your programs:

- **Stay responsive**: The user interface remains active while background work proceeds
- **Run faster**: Multiple CPU cores work on different parts of a problem simultaneously
- **Model reality**: The real world is concurrent — multiple things happen at once
- **Handle many clients**: A web server must handle thousands of users at the same time

But concurrency comes with serious challenges. When multiple things happen simultaneously, they can interfere with each other in subtle, hard-to-reproduce ways. This chapter teaches both the power and the pitfalls.

---

## 24.1 Threads

A *thread* is an independent path of execution within your program. All threads share the same memory, but each has its own instruction pointer — its own "place" in the code.

Viper provides threads through the `Viper.Threads.Thread` class.

### Starting a Thread

```rust
bind Thread = Viper.Threads.Thread;

func start() {
    // Start a new thread that runs a function
    var t = Thread.Start(func() {
        Viper.Terminal.Say("Hello from another thread!");
    });

    // Wait for the thread to finish
    t.Join();
    Viper.Terminal.Say("Thread completed.");
}
```

`Thread.Start` takes a function and runs it on a new OS thread. It returns a thread handle that you use to manage the thread's lifecycle.

### Thread Properties

Every thread handle exposes useful properties:

| Property | Type | Description |
|----------|------|-------------|
| `Id` | `Integer` | OS-assigned thread identifier |
| `IsAlive` | `Boolean` | Whether the thread is still running |
| `HasError` | `Boolean` | Whether the thread terminated with an error |
| `Error` | `String` | Error message (if `HasError` is true) |

```rust
bind Thread = Viper.Threads.Thread;

func start() {
    var t = Thread.Start(func() {
        Thread.Sleep(100);  // Sleep 100 milliseconds
    });

    Viper.Terminal.Say("Thread ID: " + toString(t.Id));
    Viper.Terminal.Say("Alive? " + toString(t.IsAlive));

    t.Join();
    Viper.Terminal.Say("Still alive? " + toString(t.IsAlive));
}
```

### Joining Threads

`Join()` blocks the calling thread until the target thread finishes. Without joining, the main thread might exit before worker threads complete.

```rust
bind Thread = Viper.Threads.Thread;

func start() {
    var t = Thread.Start(func() {
        // Simulate work
        Thread.Sleep(500);
    });

    // TryJoin returns immediately: true if done, false if still running
    var done = t.TryJoin();

    // JoinFor waits up to N milliseconds: true if done, false if timed out
    var finished = t.JoinFor(1000);
}
```

### Safe Thread Operations

`StartSafe` wraps the thread body in error handling, so unhandled errors don't crash the program:

```rust
bind Thread = Viper.Threads.Thread;

func start() {
    var t = Thread.StartSafe(func() {
        // If this throws, the error is captured
        var x = 1 / 0;
    });

    t.SafeJoin();  // Safe join that won't propagate errors

    if (t.HasError) {
        Viper.Terminal.Say("Thread error: " + t.Error);
    }
}
```

### Thread Utilities

| Method | Description |
|--------|-------------|
| `Thread.Sleep(ms)` | Pause the current thread for `ms` milliseconds |
| `Thread.Yield()` | Give other threads a chance to run |

---

## 24.2 Shared State and Monitors

When threads share data, they can corrupt it. Two threads incrementing a counter simultaneously might both read the same value, both add 1, and both write back — losing an increment. This is a *race condition*.

### Monitor (Mutual Exclusion)

A `Monitor` provides mutual exclusion: only one thread can hold the monitor at a time. Other threads that try to enter will block until it's released.

```rust
bind Thread = Viper.Threads.Thread;
bind Monitor = Viper.Threads.Monitor;

var counter = 0;
var lock = new Object();  // Any object can serve as a monitor target

func incrementCounter() {
    Monitor.Enter(lock);
    counter = counter + 1;
    Monitor.Exit(lock);
}

func start() {
    var threads: List[Object] = [];

    // Spawn 10 threads, each incrementing 100 times
    for i in 0..10 {
        var t = Thread.Start(func() {
            for j in 0..100 {
                incrementCounter();
            }
        });
        threads.Push(t);
    }

    // Wait for all threads
    for t in threads {
        Thread.Join(t);
    }

    Viper.Terminal.Say("Counter: " + toString(counter));  // Should be 1000
}
```

### Monitor Methods

| Method | Description |
|--------|-------------|
| `Monitor.Enter(obj)` | Acquire the monitor (blocks if held) |
| `Monitor.TryEnter(obj)` | Try to acquire without blocking (returns `Boolean`) |
| `Monitor.TryEnterFor(obj, ms)` | Try to acquire with timeout |
| `Monitor.Exit(obj)` | Release the monitor |
| `Monitor.Wait(obj)` | Release and wait for notification |
| `Monitor.WaitFor(obj, ms)` | Wait with timeout |
| `Monitor.Pause(obj)` | Notify one waiting thread |
| `Monitor.PauseAll(obj)` | Notify all waiting threads |

### Producer-Consumer with Monitor

```rust
bind Thread = Viper.Threads.Thread;
bind Monitor = Viper.Threads.Monitor;

var buffer: List[Integer] = [];
var lock = new Object();
var done = false;

func producer() {
    for i in 1..=10 {
        Monitor.Enter(lock);
        buffer.Push(i);
        Monitor.Pause(lock);    // Wake a waiting consumer
        Monitor.Exit(lock);
        Thread.Sleep(50);
    }

    Monitor.Enter(lock);
    done = true;
    Monitor.PauseAll(lock);     // Wake all consumers
    Monitor.Exit(lock);
}

func consumer(id: Integer) {
    while (true) {
        Monitor.Enter(lock);
        while (buffer.count() == 0 && !done) {
            Monitor.Wait(lock);  // Release lock and sleep until notified
        }
        if (buffer.count() > 0) {
            var item = buffer.get(0);
            buffer.RemoveAt(0);
            Monitor.Exit(lock);
            Viper.Terminal.Say("Consumer " + toString(id) + " got: " + toString(item));
        } else {
            Monitor.Exit(lock);
            if (done) { break; }
        }
    }
}
```

### SafeI64 (Atomic Integer)

For simple numeric shared state, `SafeI64` provides lock-free atomic operations:

```rust
bind Thread = Viper.Threads.Thread;
bind SafeI64 = Viper.Threads.SafeI64;

func start() {
    var counter = SafeI64.New(0);  // Create atomic counter starting at 0

    var t1 = Thread.Start(func() {
        for i in 0..1000 {
            counter.Add(1);  // Atomic increment
        }
    });

    var t2 = Thread.Start(func() {
        for i in 0..1000 {
            counter.Add(1);
        }
    });

    t1.Join();
    t2.Join();

    Viper.Terminal.Say("Counter: " + toString(counter.Get()));  // Always 2000
}
```

| Method | Description |
|--------|-------------|
| `SafeI64.New(initial)` | Create with initial value |
| `.Get()` | Read current value atomically |
| `.Set(value)` | Write value atomically |
| `.Add(delta)` | Add and return new value atomically |
| `.CompareExchange(expected, desired)` | CAS: set to `desired` if current equals `expected`, returns old value |

The `CompareExchange` operation (CAS) is the building block for lock-free algorithms:

```rust
bind SafeI64 = Viper.Threads.SafeI64;

// Lock-free maximum update
func atomicMax(atom: SafeI64, candidate: Integer) {
    while (true) {
        var current = atom.Get();
        if (candidate <= current) { break; }
        var old = atom.CompareExchange(current, candidate);
        if (old == current) { break; }  // Success
        // Another thread changed it — retry
    }
}
```

---

## 24.3 Synchronization Primitives

Beyond monitors, Viper provides specialized synchronization tools for different coordination patterns.

### Gate (Semaphore)

A `Gate` controls access to a limited resource. It maintains a count of available permits. Threads enter (consuming a permit) and leave (releasing a permit).

```rust
bind Thread = Viper.Threads.Thread;
bind Gate = Viper.Threads.Gate;

func start() {
    // Allow up to 3 concurrent database connections
    var dbGate = Gate.New(3);

    for i in 0..10 {
        Thread.Start(func() {
            dbGate.Enter();              // Wait for a permit
            Viper.Terminal.Say("Query " + toString(i) + " running");
            Thread.Sleep(200);           // Simulate query
            dbGate.Leave();              // Release permit
        });
    }
}
```

| Method | Description |
|--------|-------------|
| `Gate.New(permits)` | Create with N initial permits |
| `.Enter()` | Acquire one permit (blocks if none available) |
| `.TryEnter()` | Try without blocking |
| `.TryEnterFor(ms)` | Try with timeout |
| `.Leave()` | Release one permit |
| `.Leave(n)` | Release N permits |
| `.Permits` | Current available permit count |

### Barrier

A `Barrier` makes multiple threads wait until all of them have arrived at a synchronization point before any can proceed. Useful for phased algorithms.

```rust
bind Thread = Viper.Threads.Thread;
bind Barrier = Viper.Threads.Barrier;

func start() {
    var barrier = Barrier.New(3);  // Wait for 3 threads

    for i in 0..3 {
        Thread.Start(func() {
            Viper.Terminal.Say("Thread " + toString(i) + " phase 1 done");
            barrier.Arrive();  // Wait until all 3 threads arrive
            Viper.Terminal.Say("Thread " + toString(i) + " phase 2 starting");
        });
    }
}
```

| Method/Property | Description |
|-----------------|-------------|
| `Barrier.New(parties)` | Create for N parties |
| `.Arrive()` | Arrive and wait; returns arrival index |
| `.Reset()` | Reset for reuse |
| `.Parties` | Total parties expected |
| `.Waiting` | Number currently waiting |

### RwLock (Reader-Writer Lock)

A `RwLock` allows multiple simultaneous readers *or* one exclusive writer. This is more efficient than a Monitor when reads vastly outnumber writes.

```rust
bind Thread = Viper.Threads.Thread;
bind RwLock = Viper.Threads.RwLock;

var cache: List[String] = [];
var rwLock = RwLock.New();

func readCache(index: Integer) -> String {
    rwLock.ReadEnter();
    var result = cache.get(index);
    rwLock.ReadExit();
    return result;
}

func writeCache(value: String) {
    rwLock.WriteEnter();
    cache.Push(value);
    rwLock.WriteExit();
}
```

| Method/Property | Description |
|-----------------|-------------|
| `RwLock.New()` | Create a new reader-writer lock |
| `.ReadEnter()` / `.ReadExit()` | Acquire/release read access |
| `.WriteEnter()` / `.WriteExit()` | Acquire/release write access |
| `.TryReadEnter()` | Try read lock without blocking |
| `.TryWriteEnter()` | Try write lock without blocking |
| `.Readers` | Number of current readers |
| `.IsWriteLocked` | Whether a writer holds the lock |

---

## 24.4 Channels

Channels provide safe communication between threads without shared mutable state. One thread *sends* data into the channel; another thread *receives* it. This is the "communicate by sharing" approach to concurrency.

```rust
bind Thread = Viper.Threads.Thread;
bind Channel = Viper.Threads.Channel;

func start() {
    var ch = Channel.New(5);  // Buffered channel with capacity 5

    // Producer thread
    var producer = Thread.Start(func() {
        for i in 1..=10 {
            ch.Send(i);
            Viper.Terminal.Say("Sent: " + toString(i));
        }
        ch.Close();
    });

    // Consumer thread
    var consumer = Thread.Start(func() {
        while (!ch.IsClosed || !ch.IsEmpty) {
            var item = ch.Recv();
            if (item != null) {
                Viper.Terminal.Say("Received: " + toString(item));
            }
        }
    });

    producer.Join();
    consumer.Join();
}
```

### Channel Methods

| Method | Description |
|--------|-------------|
| `Channel.New(capacity)` | Create a buffered channel |
| `.Send(item)` | Send an item (blocks if full) |
| `.TrySend(item)` | Try to send without blocking |
| `.SendFor(item, ms)` | Send with timeout |
| `.Recv()` | Receive an item (blocks if empty) |
| `.TryRecv(out)` | Try to receive without blocking |
| `.RecvFor(out, ms)` | Receive with timeout |
| `.Close()` | Close the channel (no more sends) |

### Channel Properties

| Property | Type | Description |
|----------|------|-------------|
| `Length` | `Integer` | Number of items currently buffered |
| `Cap` | `Integer` | Maximum capacity |
| `IsClosed` | `Boolean` | Whether the channel has been closed |
| `IsEmpty` | `Boolean` | Whether the buffer is empty |
| `IsFull` | `Boolean` | Whether the buffer is at capacity |

### Pipeline Pattern

Channels naturally compose into pipelines where each stage processes data and passes results to the next:

```rust
bind Thread = Viper.Threads.Thread;
bind Channel = Viper.Threads.Channel;

func start() {
    var raw = Channel.New(10);
    var processed = Channel.New(10);

    // Stage 1: Generate data
    Thread.Start(func() {
        for i in 1..=5 {
            raw.Send(i);
        }
        raw.Close();
    });

    // Stage 2: Process (double each value)
    Thread.Start(func() {
        while (!raw.IsClosed || !raw.IsEmpty) {
            var item = raw.Recv();
            if (item != null) {
                processed.Send(item * 2);
            }
        }
        processed.Close();
    });

    // Stage 3: Consume results
    while (!processed.IsClosed || !processed.IsEmpty) {
        var result = processed.Recv();
        if (result != null) {
            Viper.Terminal.Say("Result: " + toString(result));
        }
    }
}
```

---

## 24.5 Thread Pools

Creating a new OS thread for every task is expensive. A *thread pool* maintains a set of reusable worker threads. You submit tasks; the pool assigns them to available workers.

```rust
bind Pool = Viper.Threads.Pool;

func start() {
    var pool = Pool.New(4);  // 4 worker threads

    // Submit 20 tasks to the pool
    for i in 0..20 {
        pool.Submit(func() {
            Viper.Terminal.Say("Task " + toString(i) + " running");
            Thread.Sleep(100);
        });
    }

    pool.Wait();      // Wait for all tasks to complete
    pool.Shutdown();   // Clean up pool resources

    Viper.Terminal.Say("All tasks done.");
    Viper.Terminal.Say("Pool processed " + toString(pool.Size) + " workers.");
}
```

### Pool Methods

| Method | Description |
|--------|-------------|
| `Pool.New(size)` | Create pool with N worker threads |
| `.Submit(func)` | Submit a task for execution |
| `.Wait()` | Block until all submitted tasks complete |
| `.WaitFor(ms)` | Wait with timeout |
| `.Shutdown()` | Graceful shutdown (finishes pending tasks) |
| `.ShutdownNow()` | Immediate shutdown (drops pending tasks) |

### Pool Properties

| Property | Type | Description |
|----------|------|-------------|
| `Size` | `Integer` | Number of worker threads |
| `Pending` | `Integer` | Tasks waiting to execute |
| `Active` | `Integer` | Tasks currently executing |
| `IsShutdown` | `Boolean` | Whether shutdown was requested |

---

## 24.6 Promises and Futures

A `Promise` represents a value that will be provided later. A `Future` is the read-side of a promise — it lets another thread wait for and retrieve the result.

```rust
bind Thread = Viper.Threads.Thread;
bind Promise = Viper.Threads.Promise;

func start() {
    var p = Promise.New();
    var f = p.GetFuture();

    // Worker thread computes a result
    Thread.Start(func() {
        Thread.Sleep(200);  // Simulate computation
        p.Set(42);          // Fulfill the promise
    });

    // Main thread waits for the result
    var result = f.Get();  // Blocks until the promise is fulfilled
    Viper.Terminal.Say("Got: " + toString(result));  // "Got: 42"
}
```

### Error Propagation

Promises can propagate errors to futures:

```rust
bind Thread = Viper.Threads.Thread;
bind Promise = Viper.Threads.Promise;

func start() {
    var p = Promise.New();
    var f = p.GetFuture();

    Thread.Start(func() {
        p.SetError("computation failed");
    });

    f.Wait();
    if (f.IsError) {
        Viper.Terminal.Say("Error: " + f.Error);
    }
}
```

### Future Methods and Properties

| Method | Description |
|--------|-------------|
| `.Get()` | Block and retrieve the value |
| `.TryGet()` | Non-blocking get (returns null if not ready) |
| `.GetFor(ms)` | Get with timeout |
| `.Wait()` | Block until resolved (value or error) |
| `.WaitFor(ms)` | Wait with timeout |

| Property | Type | Description |
|----------|------|-------------|
| `IsDone` | `Boolean` | Whether the promise has been fulfilled |
| `IsError` | `Boolean` | Whether it resolved with an error |
| `Error` | `String` | Error message (if `IsError`) |

### Async Combinators

The `Async` class provides higher-level operations built on futures:

```rust
bind Async = Viper.Threads.Async;

func start() {
    // Run a function asynchronously, get a Future back
    var f = Async.Run(func() {
        return computeExpensiveValue();
    });

    // Create a delayed future (resolves after N ms)
    var delayed = Async.Delay(1000);

    // Wait for ALL futures in a list
    var results = Async.All(futures);

    // Wait for ANY future (first to complete)
    var first = Async.Any(futures);
}
```

| Method | Description |
|--------|-------------|
| `Async.Run(func)` | Run asynchronously, returns a `Future` |
| `Async.RunCancellable(func, token)` | Run with cancellation support |
| `Async.Delay(ms)` | Future that resolves after N milliseconds |
| `Async.All(futures)` | Future that resolves when all complete |
| `Async.Any(futures)` | Future that resolves when any completes |
| `Async.Map(future, func)` | Transform a future's value |

---

## 24.7 Concurrent Collections

Regular collections (`List`, `Map`) are **not** thread-safe. Accessing them from multiple threads without locks leads to corruption. Viper provides two thread-safe collections.

### ConcurrentQueue

A thread-safe FIFO queue, ideal for producer-consumer patterns:

```rust
bind Thread = Viper.Threads.Thread;
bind ConcurrentQueue = Viper.Threads.ConcurrentQueue;

func start() {
    var queue = ConcurrentQueue.New();

    // Producer
    Thread.Start(func() {
        for i in 1..=10 {
            queue.Enqueue(i);
            Thread.Sleep(50);
        }
    });

    // Consumer
    Thread.Start(func() {
        for i in 0..10 {
            var item = queue.Dequeue();  // Blocks if empty
            Viper.Terminal.Say("Got: " + toString(item));
        }
    });
}
```

| Method | Description |
|--------|-------------|
| `ConcurrentQueue.New()` | Create an empty queue |
| `.Enqueue(item)` | Add to the back |
| `.Dequeue()` | Remove from front (blocks if empty) |
| `.TryDequeue()` | Non-blocking dequeue (returns null if empty) |
| `.DequeueTimeout(ms)` | Dequeue with timeout |
| `.Peek()` | Look at front without removing |
| `.Clear()` | Remove all items |
| `.Length` | Number of items |
| `.IsEmpty` | Whether empty |

### ConcurrentMap

A thread-safe key-value store with string keys:

```rust
bind Thread = Viper.Threads.Thread;
bind ConcurrentMap = Viper.Threads.ConcurrentMap;

func start() {
    var config = ConcurrentMap.New();

    // Writer thread
    Thread.Start(func() {
        config.Set("host", "localhost");
        config.Set("port", "8080");
    });

    // Reader thread (safe to read concurrently)
    Thread.Start(func() {
        var host = config.GetOr("host", "127.0.0.1");
        Viper.Terminal.Say("Connecting to " + host);
    });
}
```

| Method | Description |
|--------|-------------|
| `ConcurrentMap.New()` | Create an empty map |
| `.Set(key, value)` | Insert or update |
| `.Get(key)` | Retrieve value (null if missing) |
| `.GetOr(key, default)` | Retrieve with default fallback |
| `.Has(key)` | Check if key exists |
| `.SetIfMissing(key, value)` | Set only if key doesn't exist (returns true if set) |
| `.Remove(key)` | Remove a key (returns true if found) |
| `.Clear()` | Remove all entries |
| `.Keys()` | Get all keys |
| `.Values()` | Get all values |
| `.Length` | Number of entries |
| `.IsEmpty` | Whether empty |

---

## 24.8 Parallel Utilities

The `Parallel` class provides high-level utilities for common parallel patterns without manual thread management:

```rust
bind Parallel = Viper.Threads.Parallel;

func start() {
    // Parallel for loop (partitioned across cores)
    Parallel.For(0, 1000, func(i: Integer) {
        // Process item i
    });

    // Parallel for-each over a collection
    var items: List[String] = ["a", "b", "c", "d"];
    Parallel.ForEach(items, func(item: String) {
        Viper.Terminal.Say("Processing: " + item);
    });

    // Parallel map: transform each element concurrently
    var results = Parallel.Map(items, func(item: String) {
        return item + "!";
    });
}
```

### Using a Custom Pool

By default, `Parallel` uses a shared default pool. You can provide your own:

```rust
bind Parallel = Viper.Threads.Parallel;
bind Pool = Viper.Threads.Pool;

func start() {
    var pool = Pool.New(2);  // Only 2 workers

    Parallel.ForPool(0, 100, func(i: Integer) {
        // Runs on the custom pool
    }, pool);

    pool.Shutdown();
}
```

### Parallel Methods

| Method | Description |
|--------|-------------|
| `Parallel.For(start, end, func)` | Parallel for-loop over range |
| `Parallel.ForPool(start, end, func, pool)` | Same, with custom pool |
| `Parallel.ForEach(collection, func)` | Apply function to each item |
| `Parallel.ForEachPool(collection, func, pool)` | Same, with custom pool |
| `Parallel.Map(collection, func)` | Transform each item in parallel |
| `Parallel.MapPool(collection, func, pool)` | Same, with custom pool |
| `Parallel.Invoke(funcs)` | Run multiple functions in parallel |
| `Parallel.InvokePool(funcs, pool)` | Same, with custom pool |
| `Parallel.Reduce(collection, func, initial)` | Parallel reduction |
| `Parallel.ReducePool(collection, func, initial, pool)` | Same, with custom pool |
| `Parallel.DefaultWorkers()` | Number of default worker threads |
| `Parallel.DefaultPool()` | Access the shared default pool |

---

## 24.9 Cancellation

Long-running tasks should be cancellable. The `CancelToken` provides cooperative cancellation — a way for one part of the program to signal "stop" and for the running task to check periodically and comply.

```rust
bind Thread = Viper.Threads.Thread;
bind CancelToken = Viper.Threads.CancelToken;

func start() {
    var token = CancelToken.New();

    var worker = Thread.Start(func() {
        for i in 0..1000000 {
            // Periodically check for cancellation
            if (token.IsCancelled) {
                Viper.Terminal.Say("Cancelled at iteration " + toString(i));
                return;
            }
            // Do work...
        }
    });

    Thread.Sleep(100);   // Let it run a bit
    token.Cancel();      // Request cancellation
    worker.Join();
}
```

### Linked Tokens

Create a child token that cancels when the parent does:

```rust
bind CancelToken = Viper.Threads.CancelToken;

var parentToken = CancelToken.New();
var childToken = parentToken.Linked(parentToken);

parentToken.Cancel();  // Both parent and child are now cancelled
```

| Method/Property | Description |
|-----------------|-------------|
| `CancelToken.New()` | Create a new token |
| `.Cancel()` | Request cancellation |
| `.Reset()` | Reset to non-cancelled state |
| `.Check()` | Returns true if cancelled |
| `.ThrowIfCancelled()` | Throws if cancelled |
| `.Linked(parent)` | Create a child linked to parent |
| `.IsCancelled` | Whether cancellation was requested |

---

## 24.10 Rate Limiting

### Debouncer

A `Debouncer` suppresses repeated signals that arrive within a quiet period. Only after the signals stop for the configured delay does the debouncer become "ready". This is useful for UI events like search-as-you-type, where you want to wait until the user stops typing.

```rust
bind Debouncer = Viper.Threads.Debouncer;

func start() {
    var debounce = Debouncer.New(300);  // 300ms quiet period

    // Simulate rapid signals (like keystrokes)
    debounce.Signal();
    Thread.Sleep(100);
    debounce.Signal();
    Thread.Sleep(100);
    debounce.Signal();

    // Wait for the quiet period
    Thread.Sleep(400);

    if (debounce.IsReady) {
        Viper.Terminal.Say("Now execute the search!");
    }
}
```

| Method/Property | Description |
|-----------------|-------------|
| `Debouncer.New(delayMs)` | Create with quiet period |
| `.Signal()` | Register a signal (resets timer) |
| `.Reset()` | Reset state |
| `.Delay` | Configured delay in milliseconds |
| `.IsReady` | Whether the quiet period has elapsed |
| `.SignalCount` | Total signals received |

### Throttler

A `Throttler` limits how often an action can occur. At most one action per interval.

```rust
bind Throttler = Viper.Threads.Throttler;

func start() {
    var throttle = Throttler.New(1000);  // At most once per second

    for i in 0..10 {
        if (throttle.Try()) {
            Viper.Terminal.Say("Action executed at iteration " + toString(i));
        } else {
            Viper.Terminal.Say("Throttled at iteration " + toString(i));
        }
        Thread.Sleep(200);
    }
}
```

| Method/Property | Description |
|-----------------|-------------|
| `Throttler.New(intervalMs)` | Create with minimum interval |
| `.Try()` | Try to proceed (returns true if allowed) |
| `.Reset()` | Reset state |
| `.CanProceed` | Whether an action is allowed now |
| `.Count` | Total successful actions |
| `.Interval` | Configured interval in milliseconds |
| `.RemainingMs` | Milliseconds until next allowed action |

---

## 24.11 Scheduling

The `Scheduler` manages named tasks that should execute at scheduled times:

```rust
bind Scheduler = Viper.Threads.Scheduler;

func start() {
    var sched = Scheduler.New();

    // Schedule tasks by name with delay in milliseconds
    sched.Schedule("backup", 5000);      // Due in 5 seconds
    sched.Schedule("heartbeat", 1000);   // Due in 1 second

    // Poll loop
    while (sched.Pending > 0) {
        var dueTask = sched.Poll();  // Returns name of due task, or null
        if (dueTask != null) {
            Viper.Terminal.Say("Running: " + dueTask);
        }
        Thread.Sleep(100);
    }
}
```

| Method/Property | Description |
|-----------------|-------------|
| `Scheduler.New()` | Create a new scheduler |
| `.Schedule(name, delayMs)` | Schedule a named task |
| `.Cancel(name)` | Cancel a scheduled task |
| `.IsDue(name)` | Check if a task is due |
| `.Poll()` | Get next due task name (or null) |
| `.Clear()` | Cancel all tasks |
| `.Pending` | Number of scheduled tasks |

---

## 24.12 Best Practices

### 1. Minimize Shared Mutable State

The primary source of concurrency bugs is shared mutable state. Prefer:

- **Channels** for communication between threads
- **Immutable data** passed to threads at creation time
- **`SafeI64`** for simple counters
- **`ConcurrentMap`/`ConcurrentQueue`** for shared collections

### 2. Always Join Threads

Unjoined threads can outlive the main function, leading to undefined behavior or resource leaks:

```rust
// Bad: thread might not finish before program exits
Thread.Start(func() { doWork(); });

// Good: always track and join
var t = Thread.Start(func() { doWork(); });
t.Join();
```

### 3. Use StartSafe for Robustness

`Thread.StartSafe` catches errors in the thread body, preventing crashes and allowing error inspection:

```rust
var t = Thread.StartSafe(func() { riskyOperation(); });
t.SafeJoin();
if (t.HasError) {
    Viper.Terminal.Say("Thread failed: " + t.Error);
}
```

### 4. Prefer Thread Pools Over Raw Threads

Thread pools reuse threads, avoiding the overhead of creating and destroying OS threads:

```rust
// Bad: 1000 OS threads
for i in 0..1000 {
    Thread.Start(func() { processItem(i); });
}

// Good: 4 workers handling 1000 tasks
var pool = Pool.New(4);
for i in 0..1000 {
    pool.Submit(func() { processItem(i); });
}
pool.Wait();
pool.Shutdown();
```

### 5. Avoid Deadlocks

A *deadlock* occurs when two threads each wait for something the other holds. Rules to prevent deadlocks:

- Always acquire locks in the same order
- Use `TryEnter` / `TryEnterFor` with timeouts instead of blocking forever
- Keep critical sections (code between Enter/Exit) short
- Never call unknown code while holding a lock

### 6. Use Cancellation for Long Tasks

Cooperative cancellation via `CancelToken` lets you cleanly stop long-running work:

```rust
var token = CancelToken.New();

pool.Submit(func() {
    while (!token.IsCancelled) {
        processNextBatch();
    }
});

// Later: request clean shutdown
token.Cancel();
```

---

## Summary

| Concept | Class | Use Case |
|---------|-------|----------|
| Threads | `Thread` | Independent execution paths |
| Mutual exclusion | `Monitor` | Protecting shared state |
| Atomic integers | `SafeI64` | Lock-free counters |
| Semaphore | `Gate` | Limiting concurrent access |
| Barrier | `Barrier` | Phase synchronization |
| Reader-writer lock | `RwLock` | Read-heavy shared data |
| Communication | `Channel` | Thread-safe message passing |
| Task execution | `Pool` | Reusable worker threads |
| Async results | `Promise` / `Future` | Deferred values |
| Combinators | `Async` | High-level async operations |
| Safe queue | `ConcurrentQueue` | Thread-safe FIFO |
| Safe map | `ConcurrentMap` | Thread-safe key-value store |
| Parallel loops | `Parallel` | Data parallelism |
| Cancellation | `CancelToken` | Cooperative task stopping |
| Rate limiting | `Debouncer` / `Throttler` | Controlling action frequency |
| Task scheduling | `Scheduler` | Time-based task management |

All concurrency types live under `Viper.Threads`. Import them with:

```rust
bind Thread = Viper.Threads.Thread;
bind Monitor = Viper.Threads.Monitor;
bind Channel = Viper.Threads.Channel;
// ... etc.
```

Concurrency is powerful but demands discipline. Start simple — use channels and thread pools before reaching for low-level primitives. Test under load. And remember: the best concurrent code is code where threads don't share state at all.
