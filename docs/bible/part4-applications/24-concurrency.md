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
for url in imageUrls {
    var image = Http.get(url);
    images.push(image);
}
```

Most of that time is spent *waiting*. Your computer sends a request, then sits idle for perhaps 900 milliseconds while the server processes it and sends bytes across the internet. Only then does it start the next download. Your CPU, capable of billions of operations per second, twiddles its thumbs.

Now imagine downloading them in parallel:

```rust
// Parallel: ~1-2 seconds (all at once)
var tasks = [];
for url in imageUrls {
    tasks.push(Thread.spawn(func() {
        return Http.get(url);
    }));
}
for task in tasks {
    images.push(task.result());
}
```

Same work, 50-100x faster. While one download waits for network response, the others proceed. The CPU stays busy orchestrating everything.

That's the power of concurrency. It lets your programs:

- **Stay responsive**: The user interface remains active while background work proceeds
- **Run faster**: Multiple CPU cores work on different parts of a problem simultaneously
- **Model reality**: The real world is concurrent — multiple things happen at once, and programs that model real situations benefit from matching that structure
- **Handle many clients**: A web server must handle thousands of users at the same time

But concurrency comes with serious challenges. When multiple things happen simultaneously, they can interfere with each other in subtle, hard-to-reproduce ways. Programs that work perfectly 99% of the time suddenly fail. Bugs appear under heavy load and vanish when you try to debug them.

This chapter teaches both the power and the pitfalls.

---

## Mental Models for Concurrency

Before diving into code, let's build intuition. Concurrency is easier to understand through analogy.

### The Kitchen with Multiple Cooks

Imagine a restaurant kitchen. One cook could prepare an entire meal: chop vegetables, cook the protein, make the sauce, plate everything. But it would be slow.

Now imagine four cooks working together. One handles vegetables, another the protein, a third the sauces, the fourth plates and garnishes. The meal gets prepared much faster.

But coordination is crucial. If two cooks reach for the same knife, there's a problem. If one cook needs ingredients the other hasn't finished preparing, they must wait. If everyone tries to use the stove at once, chaos ensues.

Concurrency is exactly this. Multiple workers (threads) doing tasks simultaneously. Shared resources (memory, files) that require coordination. Dependencies between tasks that require proper ordering.

### Juggling Balls

A juggler keeps multiple balls in the air, but only handles one at a time. They throw one up, quickly catch another, throw it, catch the next. To an observer, it looks like everything happens simultaneously.

This is how *concurrency on a single CPU core* works. The processor rapidly switches between tasks — a few milliseconds on one, then switch to another. Each task makes progress, but only one executes at any instant.

With multiple CPU cores, true parallelism occurs — multiple balls actually in your hands at the same moment. But even then, a program with 100 tasks and 4 cores must juggle.

### The Assembly Line

A car factory uses an assembly line. Station 1 adds the frame. Station 2 installs the engine. Station 3 adds wheels. Station 4 paints. Each station works on a different car simultaneously.

This *pipeline* model is common in concurrent programs. Data flows through stages, each handled by a different thread. While one thread processes item A at stage 2, another processes item B at stage 1.

---

## Processes vs. Threads: The Conceptual Foundation

To understand concurrency in programs, you need to understand how operating systems organize running code.

### What is a Process?

When you double-click an application, the operating system creates a *process*. A process is an isolated instance of a running program. It has:

- Its own memory space (variables, data)
- Its own program counter (tracking which instruction executes next)
- Its own resources (open files, network connections)

If you open two copies of a word processor, those are two separate processes. They can't accidentally interfere with each other because each has isolated memory. If one crashes, the other continues running.

This isolation is safe but makes processes expensive. Creating a new process requires the operating system to set up all this infrastructure. Communication between processes requires special mechanisms (pipes, files, network sockets) because they can't directly share memory.

### What is a Thread?

A *thread* is a lightweight execution path within a process. Multiple threads share the same memory space but each has its own program counter — each follows its own path through the code.

Think of a process as an apartment. A thread is a person living in that apartment. Multiple people (threads) share the kitchen, bathroom, and living room (memory), but each follows their own daily schedule (execution path).

Creating threads is fast because they share the process's existing infrastructure. Communication is easy because they access the same memory. But this sharing is also dangerous — if two threads modify the same data simultaneously, corruption can occur.

### Concurrency vs. Parallelism

These terms are often confused but mean different things:

**Concurrency** is about *structure*. A concurrent program is designed to handle multiple tasks, managing their execution and coordination. Those tasks might run simultaneously, or might take turns.

**Parallelism** is about *execution*. Parallel execution means tasks literally run at the same instant, on different CPU cores.

You can have concurrency without parallelism. A single-core computer runs concurrent programs by rapidly switching between threads. The structure is concurrent; the execution is not parallel.

You can have parallelism without thoughtful concurrency. Running the same independent computation on multiple cores is parallel but doesn't involve the coordination challenges we'll discuss.

Most interesting programs are both concurrent (structured to handle multiple tasks) and parallel (executing on multiple cores). Modern computers have 4, 8, even 128 cores. Software that uses only one core wastes most of the machine's capability.

---

## The Problems Concurrency Solves

Why go through the complexity? Because concurrency solves real problems.

### Responsiveness

A photo editing application needs to stay responsive while applying filters. Without concurrency, clicking "Apply Blur" would freeze the interface for seconds. With concurrency, the filtering runs on a background thread while the main thread keeps the interface responsive. You can cancel the operation, see progress, work on other things.

```rust
func applyFilter(image: Image, filter: Filter) {
    // Show progress indicator
    progressBar.show();

    // Run expensive work in background
    var worker = Thread.spawn(func() {
        return filter.apply(image);
    });

    // Main thread stays responsive
    // User can click Cancel, resize window, etc.

    var result = worker.result();
    progressBar.hide();
    displayImage(result);
}
```

### Performance

Some computations can be split across multiple cores. Sorting a million numbers is faster if four cores each sort 250,000, then merge results. Processing 1000 images finishes in a quarter of the time with four parallel workers.

```rust
func parallelSum(numbers: [Integer]) -> Integer {
    var numCores = 4;
    var chunkSize = numbers.length / numCores;
    var threads: [Thread<Integer>] = [];

    // Launch parallel workers
    for i in 0..numCores {
        var start = i * chunkSize;
        var end = if i == numCores - 1 { numbers.length } else { (i + 1) * chunkSize };

        threads.push(Thread.spawn(func() {
            var sum = 0;
            for j in start..end {
                sum += numbers[j];
            }
            return sum;
        }));
    }

    // Combine results
    var total = 0;
    for thread in threads {
        total += thread.result();
    }
    return total;
}
```

### Modeling Real-World Systems

Many real systems are inherently concurrent. A web server handles many clients simultaneously. A game updates physics, AI, rendering, and networking independently. A chat application sends and receives messages concurrently.

Modeling these with concurrent code is natural. Each concern gets its own thread, mirroring the real-world structure.

### Utilizing I/O Wait Time

When a program reads from disk or network, it spends most of its time *waiting*. A database query might take 50 milliseconds, but the CPU only works for 0.1 milliseconds preparing the request and processing the response.

Without concurrency, the CPU idles during that wait. With concurrency, other threads run during the wait time, keeping the processor busy. A server that handles 1000 concurrent requests might only need 10 threads — because at any moment, most requests are waiting for I/O, and the CPU rapidly services whichever ones need attention.

---

## The Problems Concurrency Creates

With great power comes great peril. Concurrency introduces an entirely new category of bugs — ones that are subtle, intermittent, and maddening to debug.

### Race Conditions: The Fundamental Problem

A *race condition* occurs when program behavior depends on the relative timing of operations in different threads. The outcome is unpredictable.

Here's the classic example:

```rust
// DANGEROUS: Race condition!
var counter = 0;

var t1 = Thread.spawn(func() {
    for i in 0..100000 {
        counter += 1;  // Not atomic!
    }
});

var t2 = Thread.spawn(func() {
    for i in 0..100000 {
        counter += 1;  // Race!
    }
});

t1.join();
t2.join();

Terminal.Say("Counter: " + counter);
// Expected: 200000
// Actual: Something less, different each run!
```

Run this program 10 times and you'll get 10 different results. Maybe 183,247. Then 176,892. Then 191,004. Never 200,000.

Why? Because `counter += 1` looks like one operation but is actually three:

1. Read the current value of counter into a CPU register
2. Add 1 to the register
3. Write the register back to counter

When two threads execute these steps concurrently, they can interleave disastrously.

### Step-by-Step: How Race Conditions Corrupt Data

Let's trace what happens when both threads try to increment a counter that starts at 5:

```
Initial: counter = 5

Thread 1                          Thread 2
--------                          --------
Read counter (gets 5)
                                  Read counter (gets 5)
Add 1 (register = 6)
                                  Add 1 (register = 6)
Write counter (counter = 6)
                                  Write counter (counter = 6)

Final: counter = 6 (should be 7!)
```

Both threads read 5, both compute 6, both write 6. One increment was lost. This is called a *lost update*.

Over 200,000 iterations, thousands of increments get lost. The exact number varies based on timing, which varies based on what else the computer is doing, temperature, phase of the moon, cosmic rays. It's utterly unpredictable.

### The Terrifying Reality

Race conditions don't crash your program with a nice error message. They silently corrupt data. Your bank balance might be wrong. Your game might lose inventory items. Your document might lose edits.

Worse, race conditions are *intermittent*. The bug might not appear during testing because it requires specific timing. It might only manifest under heavy load, or on certain hardware, or once a month. You can't reliably reproduce it, which makes debugging nearly impossible.

This is why concurrent programming requires extreme discipline.

### Data Races vs. Race Conditions

A *data race* specifically refers to unsynchronized access where at least one access is a write. The `counter += 1` example is a data race.

A *race condition* is the broader category: any bug where behavior depends on timing. You can have race conditions even with synchronized access if the logic is wrong.

```rust
// No data race (proper locking) but still a race condition
func transfer(from: Account, to: Account, amount: Number) {
    from.mutex.lock();
    if from.balance >= amount {
        from.balance -= amount;
        from.mutex.unlock();

        to.mutex.lock();
        to.balance += amount;
        to.mutex.unlock();
    } else {
        from.mutex.unlock();
    }
}
```

The locks prevent data corruption, but there's a window between unlocking `from` and locking `to` where another thread might see inconsistent state (money subtracted from `from` but not yet added to `to`). The total money in the system temporarily decreases, which might cause other code to make wrong decisions.

---

## Threads: Parallel Execution

Now let's learn to write concurrent code, starting with threads.

A *thread* is an independent sequence of execution. Your program starts with one thread (the main thread). You can create more.

```rust
bind Viper.Threading;
bind Viper.Terminal;
bind Viper.Time;

func start() {
    Terminal.Say("Main thread starting");

    var thread = Thread.spawn(func() {
        Terminal.Say("Worker thread running");
        Time.sleep(1000);
        Terminal.Say("Worker thread done");
    });

    Terminal.Say("Main thread continues");

    thread.join();  // Wait for worker to finish

    Terminal.Say("All done");
}
```

`Thread.spawn` creates a new thread that runs the provided function. The main thread continues immediately — it doesn't wait for the worker.

`thread.join()` blocks until the thread completes. Without it, the main thread might exit while the worker is still running.

### Unpredictable Interleaving

Output might be:
```
Main thread starting
Main thread continues
Worker thread running
Worker thread done
All done
```

Or:
```
Main thread starting
Worker thread running
Main thread continues
Worker thread done
All done
```

The order varies because threads run independently. The operating system schedules them unpredictably.

This unpredictability is *fundamental*. You cannot control or rely on the exact interleaving of thread operations. Your code must be correct regardless of which thread runs when.

### Threads with Return Values

Threads can return values:

```rust
var thread = Thread.spawn(func() -> Integer {
    var sum = 0;
    for i in 0..1000000 {
        sum += i;
    }
    return sum;
});

// Do other work here while thread computes...
prepareOutput();

var result = thread.result();  // Waits and gets result
Terminal.Say("Sum: " + result);
```

`thread.result()` blocks until the thread finishes, then returns whatever the thread's function returned.

### Multiple Threads: Dividing Work

Here's a practical example — computing a sum in parallel:

```rust
func processChunk(data: [Integer], start: Integer, end: Integer) -> Integer {
    var sum = 0;
    for i in start..end {
        sum += data[i];
    }
    return sum;
}

func parallelSum(data: [Integer]) -> Integer {
    var numThreads = 4;
    var chunkSize = data.length / numThreads;
    var threads: [Thread<Integer>] = [];

    // Launch threads
    for i in 0..numThreads {
        var start = i * chunkSize;
        var end = if i == numThreads - 1 { data.length } else { (i + 1) * chunkSize };

        threads.push(Thread.spawn(func() {
            return processChunk(data, start, end);
        }));
    }

    // Gather results
    var total = 0;
    for thread in threads {
        total += thread.result();
    }

    return total;
}
```

Each thread processes an independent chunk. No shared mutable state, so no race conditions. The final combination happens in the main thread after all workers finish.

---

## Synchronization: Protecting Shared Data

When threads must share mutable data, you need *synchronization* — mechanisms that coordinate access.

### Mutexes: The Fundamental Lock

A *mutex* (mutual exclusion) ensures only one thread accesses a protected resource at a time. Think of it as a bathroom lock. When someone's inside, the door is locked, and others must wait.

```rust
bind Viper.Threading;

var counter = 0;
var mutex = Mutex.create();

var t1 = Thread.spawn(func() {
    for i in 0..100000 {
        mutex.lock();    // Acquire the lock
        counter += 1;    // Only one thread can be here
        mutex.unlock();  // Release the lock
    }
});

var t2 = Thread.spawn(func() {
    for i in 0..100000 {
        mutex.lock();
        counter += 1;
        mutex.unlock();
    }
});

t1.join();
t2.join();

Terminal.Say("Counter: " + counter);  // Always 200000
```

When a thread calls `mutex.lock()`:
- If the mutex is unlocked, the thread acquires it and continues
- If another thread holds it, this thread *blocks* (waits) until the mutex is released

The section of code between `lock()` and `unlock()` is called the *critical section*. Only one thread can execute a critical section protected by a given mutex.

### Visualizing Mutex Protection

```
Thread 1                          Thread 2
--------                          --------
lock() - acquires mutex
  read counter (5)                lock() - WAITS (mutex held)
  add 1
  write counter (6)               ...waiting...
unlock()                          ...waiting...
                                  lock() - acquires mutex
                                    read counter (6)
lock() - WAITS                      add 1
                                    write counter (7)
...waiting...                     unlock()
lock() - acquires mutex
...
```

The mutex serializes access. Each read-modify-write completes before another begins. No lost updates.

### The `synchronized` Block: Safer Locking

Forgetting to unlock is a common bug that freezes your program. The `synchronized` block automatically unlocks when it exits, even if an error occurs:

```rust
var mutex = Mutex.create();

// Automatically unlocks when block exits
mutex.synchronized(func() {
    counter += 1;
});
```

This is equivalent to:
```rust
mutex.lock();
try {
    counter += 1;
} finally {
    mutex.unlock();
}
```

Always prefer `synchronized` blocks over manual lock/unlock.

### What Mutexes Actually Protect

A common misconception: the mutex doesn't protect the *variable* — it protects the *code*. A mutex only works if *all* code that accesses the shared data uses the *same* mutex.

```rust
// BROKEN: Two different mutexes protecting same data
var counter = 0;
var mutex1 = Mutex.create();
var mutex2 = Mutex.create();  // Different mutex!

var t1 = Thread.spawn(func() {
    mutex1.lock();
    counter += 1;
    mutex1.unlock();
});

var t2 = Thread.spawn(func() {
    mutex2.lock();  // This doesn't wait for mutex1!
    counter += 1;
    mutex2.unlock();
});
// Still a race condition!
```

Each thread acquires its own lock, so they run simultaneously. You must use the *same* mutex everywhere you access the shared data.

---

## Atomic Operations: Lock-Free Speed

For simple operations like incrementing a counter, mutexes have overhead. *Atomic operations* provide thread-safe operations without locks.

An atomic operation completes as an indivisible unit. No thread can see it "half done." The hardware guarantees this.

```rust
bind Viper.Threading;

var counter = Atomic<Integer>.create(0);

var t1 = Thread.spawn(func() {
    for i in 0..100000 {
        counter.increment();  // Atomic - no lock needed
    }
});

var t2 = Thread.spawn(func() {
    for i in 0..100000 {
        counter.increment();
    }
});

t1.join();
t2.join();

Terminal.Say("Counter: " + counter.get());  // Always 200000
```

Atomic operations available:
- `increment()`, `decrement()`
- `add(value)`, `subtract(value)`
- `get()`, `set(value)`
- `compareAndSwap(expected, new)` — set to new value only if current value equals expected

### When to Use Atomics vs. Mutexes

Use atomics for:
- Simple counters
- Flags (on/off switches)
- Single values that change independently

Use mutexes for:
- Complex data structures
- Operations involving multiple variables
- When you need to read, compute, then write (unless using compare-and-swap)

```rust
// Atomic is perfect for a simple counter
var visitCount = Atomic<Integer>.create(0);
visitCount.increment();

// Mutex needed for complex update
var account = Account { balance: 1000, lastTransaction: null };
var accountMutex = Mutex.create();

accountMutex.synchronized(func() {
    account.balance -= 100;
    account.lastTransaction = Time.now();  // Must update together
});
```

---

## Thread-Safe Collections

Viper provides collections that handle synchronization internally:

```rust
bind Viper.Threading;

var safeList = ConcurrentList<String>.create();

// Multiple threads can safely add items
Thread.spawn(func() {
    safeList.add("item1");
});

Thread.spawn(func() {
    safeList.add("item2");
});
```

These collections use internal locking or lock-free algorithms. They're convenient, but understand their limitations:

```rust
// STILL A RACE CONDITION despite thread-safe collection
if !safeList.contains("item") {
    safeList.add("item");  // Another thread might add between check and add!
}
```

The collection protects individual operations, not sequences of operations. For check-then-act patterns, you still need external synchronization.

---

## Communication: Channels

There's a philosophy in concurrent programming: "Don't communicate by sharing memory; share memory by communicating."

Instead of multiple threads accessing shared data (with all the synchronization complexity), have threads send messages to each other. This is what *channels* provide.

```rust
bind Viper.Threading;
bind Viper.Terminal;
bind Viper.Time;

func start() {
    var channel = Channel<String>.create();

    // Producer thread
    var producer = Thread.spawn(func() {
        for i in 0..10 {
            channel.send("Message " + i);
            Time.sleep(100);
        }
        channel.close();
    });

    // Consumer thread
    var consumer = Thread.spawn(func() {
        while true {
            var message = channel.receive();
            if message == null {
                break;  // Channel closed
            }
            Terminal.Say("Got: " + message);
        }
    });

    producer.join();
    consumer.join();
}
```

### How Channels Work

A channel is like a pipe. One end sends data, the other receives. The channel handles all synchronization internally.

- `channel.send(value)` puts a value into the channel
- `channel.receive()` takes a value out (waits if empty)
- `channel.close()` signals no more data is coming

### Buffered vs. Unbuffered Channels

```rust
// Unbuffered: send blocks until receive
var channel = Channel<Integer>.create();

// Buffered: can hold 10 items before blocking
var buffered = Channel<Integer>.create(10);
```

An unbuffered channel synchronizes sender and receiver — the sender blocks until someone receives. This is *rendezvous* style communication.

A buffered channel allows the sender to continue (up to the buffer size) without waiting. Useful when producer is sometimes faster than consumer.

### Select: Waiting on Multiple Channels

Sometimes you need to receive from whichever channel has data first:

```rust
var chan1 = Channel<String>.create();
var chan2 = Channel<String>.create();

// In another thread: sending to chan1
// In another thread: sending to chan2

// Wait for whichever channel has data first
var result = Channel.select([chan1, chan2]);
Terminal.Say("Got from channel " + result.index + ": " + result.value);
```

### Why Channels Are Safer

Channels eliminate shared mutable state. Each piece of data is owned by one thread at a time. When sent through a channel, ownership transfers. No two threads access the same data simultaneously.

This doesn't make race conditions impossible (you can still have logic races), but it eliminates the most common source: data races on shared memory.

---

## Semaphores: Counting Locks

A mutex allows one thread in. A *semaphore* allows N threads in. Think of it as a limited parking lot with N spaces.

```rust
bind Viper.Threading;

// Allow at most 3 concurrent database connections
var dbSemaphore = Semaphore.create(3);

func queryDatabase(query: String) -> Result {
    dbSemaphore.acquire();  // Wait for a "permit"
    try {
        var connection = Database.connect();
        var result = connection.execute(query);
        connection.close();
        return result;
    } finally {
        dbSemaphore.release();  // Return the "permit"
    }
}
```

Even if 100 threads call `queryDatabase`, only 3 will be inside the database section at once. The rest wait for a permit to become available.

### Common Semaphore Uses

- Limiting concurrent connections (databases, APIs)
- Rate limiting (only N requests per second)
- Resource pools (only N workers processing at once)

---

## Thread Pools: Efficient Thread Reuse

Creating threads has overhead. For many small tasks, a *thread pool* is more efficient. The pool maintains a fixed number of worker threads and assigns tasks to them.

```rust
bind Viper.Threading;
bind Viper.Terminal;

func start() {
    // Pool with 4 worker threads
    var pool = ThreadPool.create(4);

    // Submit 100 tasks
    for i in 0..100 {
        pool.submit(func() {
            var result = expensiveCalculation(i);
            Terminal.Say("Task " + i + " result: " + result);
        });
    }

    // Wait for all tasks to complete
    pool.waitAll();
    pool.shutdown();
}
```

Instead of creating 100 threads (expensive), the pool's 4 workers process tasks from a queue. As each task finishes, the worker picks up the next.

### Futures: Getting Results from Pool Tasks

```rust
var pool = ThreadPool.create(4);

var futures: [Future<Integer>] = [];
for i in 0..10 {
    var future = pool.submitWithResult(func() -> Integer {
        return expensiveCalculation(i);
    });
    futures.push(future);
}

// Collect results
for future in futures {
    var result = future.get();  // Blocks until ready
    Terminal.Say("Result: " + result);
}
```

A `Future` represents a value that will be available later. `future.get()` blocks until the computation completes, then returns the result.

---

## Async/Await: Simpler I/O Concurrency

For I/O-bound work (network, files), async/await offers a cleaner model than manual threads:

```rust
bind Viper.Async;

async func fetchData(url: String) -> String {
    var response = await Http.getAsync(url);
    return response.body;
}

async func processUrls(urls: [String]) {
    // Fetch all in parallel
    var tasks = [];
    for url in urls {
        tasks.push(fetchData(url));
    }

    // Wait for all to complete
    var results = await Async.all(tasks);

    for result in results {
        Terminal.Say("Got: " + result.length + " bytes");
    }
}

func start() {
    var urls = [
        "https://api.example.com/data1",
        "https://api.example.com/data2",
        "https://api.example.com/data3"
    ];

    Async.run(processUrls(urls));
}
```

The `async` keyword marks a function that can pause. The `await` keyword pauses until an async operation completes, allowing other code to run meanwhile.

Under the hood, async/await typically uses fewer threads than explicit threading. Multiple async functions can share a single thread, with the runtime switching between them during I/O waits.

---

## Deadlocks: The Deadly Embrace

A *deadlock* occurs when threads wait for each other forever. Neither can proceed because each holds something the other needs.

```rust
bind Viper.Time;

// DEADLOCK EXAMPLE - DON'T DO THIS
var mutex1 = Mutex.create();
var mutex2 = Mutex.create();

var t1 = Thread.spawn(func() {
    mutex1.lock();           // T1 holds mutex1
    Time.sleep(100);         // Small delay makes deadlock likely
    mutex2.lock();           // T1 wants mutex2... but T2 has it!
    // Never reaches here
    mutex2.unlock();
    mutex1.unlock();
});

var t2 = Thread.spawn(func() {
    mutex2.lock();           // T2 holds mutex2
    Time.sleep(100);
    mutex1.lock();           // T2 wants mutex1... but T1 has it!
    // Never reaches here
    mutex1.unlock();
    mutex2.unlock();
});

t1.join();  // Waits forever
t2.join();
```

### Visualizing Deadlock

```
Time  Thread 1                    Thread 2
----  --------                    --------
0     lock(mutex1) - acquired     lock(mutex2) - acquired
1     ...                         ...
2     lock(mutex2) - BLOCKED      lock(mutex1) - BLOCKED
      (mutex2 held by T2)         (mutex1 held by T1)

      DEADLOCK: Both threads blocked forever
```

T1 has mutex1 and needs mutex2. T2 has mutex2 and needs mutex1. Neither can release what they hold because they're blocked trying to acquire what the other holds.

### The Four Conditions for Deadlock

Deadlock requires all four conditions:

1. **Mutual exclusion**: Resources can only be held by one thread
2. **Hold and wait**: Threads hold resources while waiting for more
3. **No preemption**: Resources can't be forcibly taken away
4. **Circular wait**: A cycle exists in who's waiting for whom

Break any condition and deadlock becomes impossible.

### Preventing Deadlocks

**Lock ordering**: Always acquire locks in a consistent order. If everyone locks mutex1 before mutex2, circular wait is impossible.

```rust
// Always lock in consistent order (by ID, alphabetically, etc.)
func transferMoney(from: Account, to: Account, amount: Number) {
    // Lock lower ID first, always
    var first = if from.id < to.id { from } else { to };
    var second = if from.id < to.id { to } else { from };

    first.mutex.lock();
    second.mutex.lock();

    from.balance -= amount;
    to.balance += amount;

    second.mutex.unlock();
    first.mutex.unlock();
}
```

**Timeout**: Use `tryLock` with a timeout. If you can't acquire a lock in time, release what you hold and retry.

```rust
func safeOperation() -> Boolean {
    if !mutex1.tryLock(1000) {  // 1 second timeout
        return false;
    }

    if !mutex2.tryLock(1000) {
        mutex1.unlock();  // Release first lock
        return false;
    }

    // Do work
    mutex2.unlock();
    mutex1.unlock();
    return true;
}
```

**Single lock**: Use one lock for related resources. Simpler but may reduce parallelism.

**Avoid nesting**: Design to minimize holding multiple locks simultaneously.

---

## Livelock and Starvation

Deadlock isn't the only way concurrency fails.

### Livelock: Busy Doing Nothing

In a livelock, threads aren't blocked but can't make progress because they keep reacting to each other.

Imagine two people in a hallway. They both step left to pass. Then both step right. Then both step left again. Forever.

```rust
// Potential livelock
while !mutex.tryLock(10) {
    // Keep retrying immediately
}
```

If two threads do this in opposite order with two mutexes, they might repeatedly acquire one, fail to get the other, release, retry — forever.

**Solution**: Add randomized delays. Instead of retrying immediately, wait a random time. This breaks the synchronization that causes livelock.

### Starvation: Never Getting a Turn

*Starvation* occurs when a thread never gets access to a resource despite it being available to others. Low-priority threads might starve if high-priority threads monopolize resources.

**Solution**: Use fair locks that serve waiters in order. Bound how long any thread can hold a resource.

---

## Common Mistakes in Concurrent Programming

### Mistake 1: Forgetting to Join Threads

```rust
// Bad: Program might exit before thread finishes
func start() {
    Thread.spawn(func() {
        doImportantWork();
    });
    // Program ends immediately!
}

// Good: Wait for thread
func start() {
    var t = Thread.spawn(func() {
        doImportantWork();
    });
    t.join();
}
```

### Mistake 2: Sharing Mutable Data Without Synchronization

```rust
// Bad: Race condition
var data = [];
Thread.spawn(func() { data.push("A"); });
Thread.spawn(func() { data.push("B"); });

// Good: Use thread-safe collection
var data = ConcurrentList<String>.create();
Thread.spawn(func() { data.add("A"); });
Thread.spawn(func() { data.add("B"); });

// Or use mutex
var data = [];
var mutex = Mutex.create();
Thread.spawn(func() {
    mutex.synchronized(func() { data.push("A"); });
});
```

### Mistake 3: Holding Locks Too Long

```rust
// Bad: Holds lock during slow I/O
mutex.lock();
var data = Http.get(slowUrl);  // Blocks other threads for seconds!
process(data);
mutex.unlock();

// Good: Only lock for shared data access
var data = Http.get(slowUrl);  // No lock needed here
mutex.lock();
sharedResults.push(data);      // Only lock for shared access
mutex.unlock();
```

### Mistake 4: Lock Ordering Inconsistency

```rust
// Bad: Different order in different places
func transferA() {
    account1.lock();
    account2.lock();  // Order: 1, 2
}

func transferB() {
    account2.lock();
    account1.lock();  // Order: 2, 1 - DEADLOCK RISK!
}
```

### Mistake 5: Forgetting to Unlock

```rust
// Bad: If error occurs, mutex stays locked forever
mutex.lock();
riskyOperation();  // What if this throws an error?
mutex.unlock();

// Good: Use synchronized block
mutex.synchronized(func() {
    riskyOperation();
});
```

### Mistake 6: Check-Then-Act Races

```rust
// Bad: Gap between check and action
if !cache.contains(key) {
    // Another thread might add key here!
    cache.set(key, computeValue());
}

// Good: Atomic check-and-set
cache.setIfAbsent(key, func() {
    return computeValue();
});
```

---

## Debugging Concurrent Programs

Concurrency bugs are notoriously hard to debug. They're intermittent, timing-dependent, and often disappear when you add debugging code (which changes timing).

### Strategy 1: Stress Testing

Run your program under heavy load. More threads, more operations, more chaos. Bugs that appear 1 in 1000 runs become 1 in 10 under stress.

```rust
func stressTest() {
    for trial in 0..1000 {
        var threads: [Thread<void>] = [];
        for i in 0..100 {
            threads.push(Thread.spawn(func() {
                // Run the suspicious code
                suspiciousOperation();
            }));
        }
        for t in threads {
            t.join();
        }
        // Verify invariants
        if !checkInvariants() {
            Terminal.Say("Bug found on trial " + trial);
            break;
        }
    }
}
```

### Strategy 2: Race Detectors

Some tools detect data races automatically by tracking memory accesses. They slow execution significantly but catch races that testing misses.

### Strategy 3: Logging with Timestamps

Add logging with thread IDs and timestamps. Reconstruct the sequence of events after a failure.

```rust
bind Viper.Terminal;

func logMessage(message: String) {
    var threadId = Thread.currentId();
    var time = Time.millis();
    Terminal.Say("[" + time + "] Thread " + threadId + ": " + message);
}
```

### Strategy 4: Minimize Shared State

The best debugging strategy is prevention. The less shared mutable state, the fewer potential bugs.

### Strategy 5: Code Review

Concurrent code needs extra review. Fresh eyes catch lock ordering issues, missing synchronization, and other subtle bugs.

---

## Best Practices for Concurrent Code

### 1. Minimize Shared Mutable State

Every piece of shared mutable state is a potential bug. Prefer:
- Immutable data (can be safely shared without locks)
- Thread-local data (each thread has its own copy)
- Message passing (data moves between threads, not shared)

### 2. When You Must Share, Use Proper Synchronization

Don't assume operations are atomic. Don't assume memory is immediately visible to other threads. Use mutexes, atomics, or thread-safe collections.

### 3. Prefer Message Passing Over Shared Memory

Channels and message queues are easier to reason about than shared data with locks.

### 4. Keep Critical Sections Short

Hold locks for the minimum time necessary. Long critical sections reduce parallelism and increase contention.

### 5. Consistent Lock Ordering

Document and enforce lock ordering. If you must hold multiple locks, always acquire them in the same order everywhere.

### 6. Use Higher-Level Abstractions

Thread pools, futures, async/await — these handle common patterns correctly. Don't reinvent synchronization unless necessary.

### 7. Test Thoroughly

Test under load. Test with more threads than cores. Test with randomized timing. Test on different machines.

### 8. Document Threading Assumptions

Comment which methods are thread-safe, which locks protect which data, what ordering guarantees exist.

---

## A Complete Example: Parallel Image Processor

Let's put it all together with a real application:

```rust
module ImageProcessor;

bind Viper.Threading;
bind Viper.Graphics;
bind Viper.File;
bind Viper.Terminal;
bind Viper.Time;

value ImageTask {
    inputPath: String;
    outputPath: String;
}

entity ParallelProcessor {
    hide pool: ThreadPool;
    hide completedCount: Atomic<Integer>;
    hide totalCount: Integer;

    expose func init(numWorkers: Integer) {
        self.pool = ThreadPool.create(numWorkers);
        self.completedCount = Atomic.create(0);
        self.totalCount = 0;
    }

    func process(tasks: [ImageTask]) {
        self.totalCount = tasks.length;
        var futures: [Future<Boolean>] = [];

        for task in tasks {
            var future = self.pool.submitWithResult(func() -> Boolean {
                return self.processImage(task);
            });
            futures.push(future);
        }

        // Progress reporting in main thread
        while self.completedCount.get() < self.totalCount {
            var done = self.completedCount.get();
            var percent = (done * 100) / self.totalCount;
            Terminal.Say("Progress: " + percent + "% (" + done + "/" + self.totalCount + ")");
            Time.sleep(500);
        }

        // Collect results
        var successCount = 0;
        for future in futures {
            if future.get() {
                successCount += 1;
            }
        }

        Terminal.Say("Completed: " + successCount + "/" + self.totalCount + " succeeded");
    }

    func processImage(task: ImageTask) -> Boolean {
        try {
            var image = Image.load(task.inputPath);

            // Apply some processing
            image = applyGrayscale(image);
            image = applyResize(image, 800, 600);

            image.save(task.outputPath);

            self.completedCount.increment();
            return true;

        } catch Error as e {
            Terminal.Say("Error processing " + task.inputPath + ": " + e.message);
            self.completedCount.increment();
            return false;
        }
    }

    func shutdown() {
        self.pool.shutdown();
    }
}

func applyGrayscale(image: Image) -> Image {
    for y in 0..image.height {
        for x in 0..image.width {
            var pixel = image.getPixel(x, y);
            var gray = (pixel.r + pixel.g + pixel.b) / 3;
            image.setPixel(x, y, Color(gray, gray, gray));
        }
    }
    return image;
}

func applyResize(image: Image, width: Integer, height: Integer) -> Image {
    return image.resize(width, height);
}

func start() {
    var files = File.listDir("input_images/");
    var tasks: [ImageTask] = [];

    for file in files {
        if file.endsWith(".jpg") || file.endsWith(".png") {
            tasks.push(ImageTask {
                inputPath: "input_images/" + file,
                outputPath: "output_images/" + file
            });
        }
    }

    Terminal.Say("Processing " + tasks.length + " images...");

    var processor = ParallelProcessor(4);  // 4 worker threads
    processor.process(tasks);
    processor.shutdown();

    Terminal.Say("Done!");
}
```

### Walking Through the Design

This example demonstrates several best practices:

1. **Thread pool** instead of creating threads per task — efficient for many small jobs

2. **Atomic counter** for progress tracking — no mutex needed for simple counter

3. **Futures** to collect success/failure results from each task

4. **No shared mutable state** between image processing tasks — each task works on independent data

5. **Progress reporting** in main thread while workers process — keeps user informed

6. **Error handling** that doesn't crash the entire program — failed images are counted and reported

---

## The Two Languages

**Zia**
```rust
bind Viper.Threading;
bind Viper.Terminal;

var thread = Thread.spawn(func() {
    Terminal.Say("In thread");
});
thread.join();

var mutex = Mutex.create();
mutex.lock();
// critical section
mutex.unlock();
```

**BASIC**
```basic
DIM t AS Thread
t = THREAD_SPAWN(MyWorker)
THREAD_JOIN t

DIM m AS Mutex
m = MUTEX_CREATE()
MUTEX_LOCK m
' critical section
MUTEX_UNLOCK m

SUB MyWorker()
    PRINT "In thread"
END SUB
```

---

## Summary

Concurrency is powerful and perilous. Here's what to remember:

- **Threads** run code in parallel, enabling responsiveness and performance
- **Race conditions** occur when thread timing affects correctness — the fundamental danger
- **Mutexes** protect critical sections, ensuring only one thread enters at a time
- **Atomics** provide fast, lock-free operations for simple cases
- **Channels** enable communication without shared memory
- **Semaphores** limit how many threads access a resource simultaneously
- **Thread pools** efficiently manage many tasks with few threads
- **Deadlocks** happen when threads wait for each other forever
- **Async/await** simplifies I/O-bound concurrency

Rules of thumb:

1. **Avoid shared mutable state when possible** — immutable data is always safe
2. **When you must share, use proper synchronization** — never access shared data unprotected
3. **Prefer message passing over shared memory** — channels are easier to reason about
4. **Keep critical sections short** — hold locks briefly
5. **Use consistent lock ordering** — prevents deadlocks
6. **Test concurrent code thoroughly** — bugs hide until stressed

Concurrency is one of the most challenging areas in programming. Even experts make mistakes. But mastering it unlocks the full power of modern hardware and enables applications that feel responsive and fast.

---

## Exercises

**Exercise 24.1 (Mimic)**: Modify the first thread example to create three worker threads instead of one. Each should print its own message (e.g., "Worker 1 running", "Worker 2 running"). Wait for all to complete.

**Exercise 24.2 (Counter)**: Write a program with a shared counter and 4 threads. Each thread increments the counter 10,000 times. First implement it without synchronization (observe the race condition), then fix it using a mutex, then fix it using an atomic.

**Exercise 24.3 (Pipeline)**: Create a producer-consumer pipeline: Thread 1 generates numbers 1-100, Thread 2 squares them, Thread 3 prints results. Use channels to connect them.

**Exercise 24.4 (Word Counter)**: Write a program that counts words in multiple files in parallel. Each file is processed by a separate thread. Combine the totals at the end.

**Exercise 24.5 (Rate Limiter)**: Using a semaphore, create a function that limits concurrent API calls to at most 5. Test by making 20 calls that each take 1 second.

**Exercise 24.6 (Parallel Search)**: Given a large array of strings, search for a target using 4 parallel threads. Each searches a quarter of the array. Return the index if found, -1 if not.

**Exercise 24.7 (Dining Philosophers)**: Implement the classic dining philosophers problem. Five philosophers sit at a table with five forks. Each needs two forks to eat. Implement without deadlock using lock ordering.

**Exercise 24.8 (Thread Pool)**: Implement a simple thread pool from scratch. It should have a fixed number of workers and a task queue. Workers pull tasks from the queue and execute them.

**Exercise 24.9 (Parallel Merge Sort)**: Implement merge sort that splits the array across threads for sorting, then merges results. Compare performance to single-threaded sort.

**Exercise 24.10 (Thread-Safe Cache)**: Build a cache with get/set operations that is thread-safe. Add expiration so entries are removed after a timeout. Handle concurrent reads efficiently (multiple readers allowed when no writer).

**Exercise 24.11 (Web Crawler)**: Build a web crawler that fetches pages in parallel but limits concurrent requests to 10. Use a semaphore for the limit and channels to coordinate.

**Exercise 24.12 (Challenge - Job Queue)**: Create a priority job queue system. Jobs have priorities 1-10. Multiple workers process jobs from the queue, always taking the highest priority available. Handle the case where high-priority jobs keep arriving (ensure low-priority jobs eventually run).

---

*We've completed Part IV! You can now build real applications: games, networked services, data processors, and more — including ones that take full advantage of modern multi-core processors.*

*Part V takes you deeper — understanding how Viper works under the hood, optimizing performance, testing your code, and designing larger systems.*

*[Continue to Part V: Mastery](../part5-mastery/25-how-viper-works.md)*
