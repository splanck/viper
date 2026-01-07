# Chapter 24: Concurrency

Modern computers have multiple CPU cores. Modern programs do multiple things: download files while updating the UI, process data while accepting new requests, run game logic while loading assets.

This chapter teaches you to write programs that do many things at once.

---

## Why Concurrency?

Consider a program that downloads 100 images:

```viper
// Sequential: ~100 seconds (1 second per image)
for url in imageUrls {
    let image = Http.get(url);
    images.push(image);
}
```

Most of that time is waiting for network responses. Your CPU sits idle.

Now imagine downloading them in parallel:

```viper
// Parallel: ~1-2 seconds (all at once)
let tasks = [];
for url in imageUrls {
    tasks.push(Thread.spawn(func() {
        return Http.get(url);
    }));
}
for task in tasks {
    images.push(task.result());
}
```

Same work, 50-100x faster. That's the power of concurrency.

---

## Threads: Parallel Execution

A *thread* is an independent sequence of execution. Your program starts with one thread. You can create more:

```viper
import Viper.Threading;

func start() {
    Viper.Terminal.Say("Main thread starting");

    let thread = Thread.spawn(func() {
        Viper.Terminal.Say("Worker thread running");
        Viper.Time.sleep(1000);
        Viper.Terminal.Say("Worker thread done");
    });

    Viper.Terminal.Say("Main thread continues");

    thread.join();  // Wait for worker to finish

    Viper.Terminal.Say("All done");
}
```

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

The order varies — that's the nature of parallel execution.

### Thread with Return Value

```viper
let thread = Thread.spawn(func() -> i64 {
    let sum = 0;
    for i in 0..1000000 {
        sum += i;
    }
    return sum;
});

// Do other work here...

let result = thread.result();  // Waits and gets result
Viper.Terminal.Say("Sum: " + result);
```

### Multiple Threads

```viper
func processChunk(data: [i64], start: i64, end: i64) -> i64 {
    let sum = 0;
    for i in start..end {
        sum += data[i];
    }
    return sum;
}

func parallelSum(data: [i64]) -> i64 {
    let numThreads = 4;
    let chunkSize = data.length / numThreads;
    let threads: [Thread<i64>] = [];

    for i in 0..numThreads {
        let start = i * chunkSize;
        let end = if i == numThreads - 1 { data.length } else { (i + 1) * chunkSize };

        threads.push(Thread.spawn(func() {
            return processChunk(data, start, end);
        }));
    }

    let total = 0;
    for thread in threads {
        total += thread.result();
    }

    return total;
}
```

---

## The Danger: Race Conditions

When threads share data, problems arise:

```viper
// DANGEROUS: Race condition!
let counter = 0;

let t1 = Thread.spawn(func() {
    for i in 0..100000 {
        counter += 1;  // Not atomic!
    }
});

let t2 = Thread.spawn(func() {
    for i in 0..100000 {
        counter += 1;  // Race!
    }
});

t1.join();
t2.join();

Viper.Terminal.Say("Counter: " + counter);
// Expected: 200000
// Actual: Something less, different each run!
```

Why? `counter += 1` is actually:
1. Read counter
2. Add 1
3. Write counter

If two threads do this simultaneously:
1. T1 reads 5
2. T2 reads 5
3. T1 writes 6
4. T2 writes 6

We lost an increment! This is a *race condition*.

---

## Synchronization: Locks and Mutexes

A *mutex* (mutual exclusion) ensures only one thread accesses shared data at a time:

```viper
import Viper.Threading;

let counter = 0;
let mutex = Mutex.create();

let t1 = Thread.spawn(func() {
    for i in 0..100000 {
        mutex.lock();
        counter += 1;
        mutex.unlock();
    }
});

let t2 = Thread.spawn(func() {
    for i in 0..100000 {
        mutex.lock();
        counter += 1;
        mutex.unlock();
    }
});

t1.join();
t2.join();

Viper.Terminal.Say("Counter: " + counter);  // Always 200000
```

### Using `synchronized` Blocks

A cleaner way to use locks:

```viper
let mutex = Mutex.create();

// Automatically unlocks when block exits
mutex.synchronized(func() {
    counter += 1;
});
```

### Thread-Safe Data Structures

Viper provides thread-safe versions of common structures:

```viper
import Viper.Threading;

let safeList = ConcurrentList<string>.create();

// Multiple threads can safely add items
Thread.spawn(func() {
    safeList.add("item1");
});

Thread.spawn(func() {
    safeList.add("item2");
});
```

---

## Atomic Operations

For simple operations, atomics are faster than mutexes:

```viper
import Viper.Threading;

let counter = Atomic<i64>.create(0);

let t1 = Thread.spawn(func() {
    for i in 0..100000 {
        counter.increment();
    }
});

let t2 = Thread.spawn(func() {
    for i in 0..100000 {
        counter.increment();
    }
});

t1.join();
t2.join();

Viper.Terminal.Say("Counter: " + counter.get());  // Always 200000
```

Atomic operations available:
- `increment()`, `decrement()`
- `add(value)`, `subtract(value)`
- `compareAndSwap(expected, new)`
- `get()`, `set(value)`

---

## Communication: Channels

Instead of sharing memory, share by communicating:

```viper
import Viper.Threading;

func start() {
    let channel = Channel<string>.create();

    // Producer thread
    let producer = Thread.spawn(func() {
        for i in 0..10 {
            channel.send("Message " + i);
            Viper.Time.sleep(100);
        }
        channel.close();
    });

    // Consumer thread
    let consumer = Thread.spawn(func() {
        while true {
            let message = channel.receive();
            if message == null {
                break;  // Channel closed
            }
            Viper.Terminal.Say("Got: " + message);
        }
    });

    producer.join();
    consumer.join();
}
```

### Buffered Channels

```viper
// Unbuffered: send blocks until receive
let channel = Channel<i64>.create();

// Buffered: can hold 10 items before blocking
let buffered = Channel<i64>.create(10);
```

### Select: Multiple Channels

```viper
let chan1 = Channel<string>.create();
let chan2 = Channel<string>.create();

// Wait for whichever channel has data first
let result = Channel.select([chan1, chan2]);
Viper.Terminal.Say("Got from channel " + result.index + ": " + result.value);
```

---

## Thread Pools

Creating threads has overhead. For many small tasks, use a pool:

```viper
import Viper.Threading;

func start() {
    // Pool with 4 worker threads
    let pool = ThreadPool.create(4);

    // Submit tasks
    for i in 0..100 {
        pool.submit(func() {
            let result = expensiveCalculation(i);
            Viper.Terminal.Say("Task " + i + " result: " + result);
        });
    }

    // Wait for all tasks to complete
    pool.waitAll();
    pool.shutdown();
}
```

### Futures and Promises

Get results from pool tasks:

```viper
let pool = ThreadPool.create(4);

let futures: [Future<i64>] = [];
for i in 0..10 {
    let future = pool.submitWithResult(func() -> i64 {
        return expensiveCalculation(i);
    });
    futures.push(future);
}

// Collect results
for future in futures {
    let result = future.get();  // Blocks until ready
    Viper.Terminal.Say("Result: " + result);
}
```

---

## Async/Await: Simpler Concurrency

For I/O-bound tasks, async/await is often cleaner:

```viper
import Viper.Async;

async func fetchData(url: string) -> string {
    let response = await Http.getAsync(url);
    return response.body;
}

async func processUrls(urls: [string]) {
    // Fetch all in parallel
    let tasks = [];
    for url in urls {
        tasks.push(fetchData(url));
    }

    // Wait for all to complete
    let results = await Async.all(tasks);

    for result in results {
        Viper.Terminal.Say("Got: " + result.length + " bytes");
    }
}

func start() {
    let urls = [
        "https://api.example.com/data1",
        "https://api.example.com/data2",
        "https://api.example.com/data3"
    ];

    Async.run(processUrls(urls));
}
```

---

## Deadlocks: The Ultimate Bug

A *deadlock* occurs when threads wait for each other forever:

```viper
// DEADLOCK EXAMPLE - DON'T DO THIS
let mutex1 = Mutex.create();
let mutex2 = Mutex.create();

let t1 = Thread.spawn(func() {
    mutex1.lock();
    Viper.Time.sleep(100);
    mutex2.lock();  // Waits forever...
    // ...
});

let t2 = Thread.spawn(func() {
    mutex2.lock();
    Viper.Time.sleep(100);
    mutex1.lock();  // Waits forever...
    // ...
});
```

T1 has mutex1, wants mutex2. T2 has mutex2, wants mutex1. Neither can proceed.

### Preventing Deadlocks

1. **Lock ordering**: Always acquire locks in the same order
2. **Timeout**: Use `tryLock` with a timeout
3. **Single lock**: Use one lock for related resources
4. **Avoid nesting**: Minimize holding multiple locks

```viper
// Lock ordering: always lock in alphabetical order by name
func transferMoney(from: Account, to: Account, amount: f64) {
    let first = if from.id < to.id { from } else { to };
    let second = if from.id < to.id { to } else { from };

    first.mutex.lock();
    second.mutex.lock();

    from.balance -= amount;
    to.balance += amount;

    second.mutex.unlock();
    first.mutex.unlock();
}
```

---

## A Complete Example: Parallel Image Processor

```viper
module ImageProcessor;

import Viper.Threading;
import Viper.Graphics;
import Viper.File;

struct ImageTask {
    inputPath: string;
    outputPath: string;
}

class ParallelProcessor {
    private pool: ThreadPool;
    private completedCount: Atomic<i64>;
    private totalCount: i64;

    constructor(numWorkers: i64) {
        self.pool = ThreadPool.create(numWorkers);
        self.completedCount = Atomic.create(0);
        self.totalCount = 0;
    }

    func process(tasks: [ImageTask]) {
        self.totalCount = tasks.length;
        let futures: [Future<bool>] = [];

        for task in tasks {
            let future = self.pool.submitWithResult(func() -> bool {
                return self.processImage(task);
            });
            futures.push(future);
        }

        // Progress reporting in main thread
        while self.completedCount.get() < self.totalCount {
            let done = self.completedCount.get();
            let percent = (done * 100) / self.totalCount;
            Viper.Terminal.Say("Progress: " + percent + "% (" + done + "/" + self.totalCount + ")");
            Viper.Time.sleep(500);
        }

        // Collect results
        let successCount = 0;
        for future in futures {
            if future.get() {
                successCount += 1;
            }
        }

        Viper.Terminal.Say("Completed: " + successCount + "/" + self.totalCount + " succeeded");
    }

    func processImage(task: ImageTask) -> bool {
        try {
            let image = Image.load(task.inputPath);

            // Apply some processing
            image = applyGrayscale(image);
            image = applyResize(image, 800, 600);

            image.save(task.outputPath);

            self.completedCount.increment();
            return true;

        } catch Error as e {
            Viper.Terminal.Say("Error processing " + task.inputPath + ": " + e.message);
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
            let pixel = image.getPixel(x, y);
            let gray = (pixel.r + pixel.g + pixel.b) / 3;
            image.setPixel(x, y, Color(gray, gray, gray));
        }
    }
    return image;
}

func applyResize(image: Image, width: i64, height: i64) -> Image {
    return image.resize(width, height);
}

func start() {
    let files = File.listDir("input_images/");
    let tasks: [ImageTask] = [];

    for file in files {
        if file.endsWith(".jpg") || file.endsWith(".png") {
            tasks.push(ImageTask {
                inputPath: "input_images/" + file,
                outputPath: "output_images/" + file
            });
        }
    }

    Viper.Terminal.Say("Processing " + tasks.length + " images...");

    let processor = ParallelProcessor(4);  // 4 worker threads
    processor.process(tasks);
    processor.shutdown();

    Viper.Terminal.Say("Done!");
}
```

---

## The Three Languages

**ViperLang**
```viper
import Viper.Threading;

let thread = Thread.spawn(func() {
    Viper.Terminal.Say("In thread");
});
thread.join();

let mutex = Mutex.create();
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

**Pascal**
```pascal
uses ViperThreading;
var
    t: TThread;
    m: TMutex;

procedure Worker;
begin
    WriteLn('In thread');
end;

begin
    t := ThreadSpawn(@Worker);
    ThreadJoin(t);

    m := MutexCreate;
    MutexLock(m);
    { critical section }
    MutexUnlock(m);
end.
```

---

## Common Mistakes

**Forgetting to join threads**
```viper
// Bad: Program might exit before thread finishes
Thread.spawn(func() {
    doImportantWork();
});
// Program ends immediately

// Good: Wait for thread
let t = Thread.spawn(func() {
    doImportantWork();
});
t.join();
```

**Sharing mutable data without locks**
```viper
// Bad: Race condition
let data = [];
Thread.spawn(func() { data.push("A"); });
Thread.spawn(func() { data.push("B"); });

// Good: Use thread-safe collection or lock
let data = ConcurrentList<string>.create();
Thread.spawn(func() { data.add("A"); });
Thread.spawn(func() { data.add("B"); });
```

**Holding locks too long**
```viper
// Bad: Holds lock during slow I/O
mutex.lock();
let data = Http.get(slowUrl);  // Blocks other threads!
process(data);
mutex.unlock();

// Good: Only lock for shared data access
let data = Http.get(slowUrl);  // No lock needed here
mutex.lock();
sharedResults.push(data);  // Only lock for shared access
mutex.unlock();
```

---

## Summary

- **Threads** run code in parallel
- **Race conditions** occur when threads access shared data unsafely
- **Mutexes** provide mutual exclusion for safe access
- **Atomics** are fast for simple operations
- **Channels** let threads communicate without sharing memory
- **Thread pools** efficiently manage many tasks
- **Deadlocks** happen when threads wait for each other forever
- **Async/await** simplifies I/O-bound concurrency

Rules of thumb:
1. Avoid shared mutable state when possible
2. When you must share, use proper synchronization
3. Prefer message passing (channels) over shared memory
4. Keep critical sections short
5. Test concurrent code thoroughly

---

## Exercises

**Exercise 24.1**: Write a program that counts words in multiple files in parallel, then sums the totals.

**Exercise 24.2**: Create a producer-consumer system: one thread generates numbers, another squares them, a third prints results.

**Exercise 24.3**: Build a simple web crawler that fetches pages in parallel (limit to 10 concurrent requests).

**Exercise 24.4**: Implement a parallel merge sort that splits the array across threads.

**Exercise 24.5**: Create a thread-safe cache with get/set operations and an expiration time.

**Exercise 24.6** (Challenge): Build a simple job queue system where multiple workers process jobs from a shared queue, with job priorities.

---

*We've completed Part IV! You can now build real applications: games, networked services, data processors, and more.*

*Part V takes you deeper — understanding how Viper works under the hood, optimizing performance, testing your code, and designing larger systems.*

*[Continue to Part V: Mastery →](../part5-mastery/25-how-viper-works.md)*
