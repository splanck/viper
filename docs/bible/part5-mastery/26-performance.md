# Chapter 26: Performance

Your program works. It does what it should. Users can log in, search for products, and place orders. But when a hundred users try to search at once, the server groans. When your game runs on an older computer, the frame rate stutters. When your data analysis script processes a million records, you wait... and wait... and wait.

Performance matters. Not everywhere, and not always from the start, but when it matters, it really matters. A fast program respects your users' time. A slow program frustrates them, wastes electricity, and sometimes simply fails to do its job. The good news? Most performance problems have straightforward solutions once you understand where to look.

This chapter teaches you to think about performance, measure it accurately, and improve it systematically. You'll learn to identify bottlenecks, understand algorithmic complexity, and apply optimization strategies that actually work. Most importantly, you'll learn when to optimize and when to leave well enough alone.

---

## When Does Performance Matter?

Before we dive into techniques, let's establish when you should even think about performance:

**Performance matters when:**
- Users are waiting (response time over ~200ms feels sluggish)
- Your program can't keep up with incoming data
- A batch job takes hours instead of minutes
- Your game drops below 30 frames per second
- Your server costs twice what it should
- Battery drain makes your mobile app unusable

**Performance doesn't matter when:**
- The program already runs fast enough
- Development time is more valuable than execution time
- The code runs rarely (once a day, once a week)
- You're still figuring out what the program should do

The key insight is that performance is a feature like any other. It competes for your attention with correctness, readability, and development speed. Sometimes it wins that competition. Often it doesn't.

---

## Mental Models for Performance

Before measuring anything, let's build intuition about how programs become slow. Three mental models will serve you well.

### The Traffic Flow Model

Imagine your program as a highway system. Data flows from input, through processing, to output. Like traffic, it can flow smoothly or get stuck in jams.

```
       ┌─────────┐      ┌────────────┐      ┌─────────┐
Input ─►  Parse  ├─────►│  Process   ├─────►│ Output  ├──► Result
       └─────────┘      └────────────┘      └─────────┘
         Fast!           Slow! Jam!           Fast!
```

When traffic jams on the highway, widening an already-fast section doesn't help. You need to find where cars are backed up. Similarly, optimizing fast code doesn't make your program faster. You must find the bottleneck.

A *bottleneck* is the narrowest point in your program's flow. Everything queues up behind it, waiting. Fix the bottleneck, and everything speeds up. Fix something else, and nothing changes.

### The Domino Effect

Sometimes slow code triggers more slow code. Reading a file slowly means processing starts late. Slow processing means the network sits idle waiting. Slow network means the user stares at a spinner.

```rust
func processReport() {
    var data = readLargeFile();     // If this takes 10 seconds...
    var analyzed = analyze(data);    // ...this waits 10 seconds to start
    sendEmail(analyzed);             // ...and this waits even longer
}
```

Speeding up the first domino can speed up the whole chain, even if later dominoes were already fast. They just couldn't start until the first one fell.

### The 80/20 Rule (Pareto Principle)

Here's a fact that surprises many programmers: in most programs, 80% of execution time is spent in 20% of the code. Sometimes it's even more extreme: 90/10 or 95/5.

This means most of your code doesn't matter for performance. You could triple the speed of 80% of your program and see almost no improvement. But double the speed of that critical 20%, and your program runs nearly twice as fast.

```
┌─────────────────────────────────────────────────────────────┐
│ Total Execution Time                                         │
├─────────────────────────────────────────────────────────────┤
│████████████████████████████████████████│░░░░░░░░░░░░░░░░░░░░│
│        processImage: 80%                │    everything else │
│                                         │         20%        │
└─────────────────────────────────────────────────────────────┘
```

This is liberating! You don't need to optimize everything. Find the 20% and focus there.

---

## The First Rule: Measure Before You Optimize

Here's the most important advice in this chapter: **never optimize based on guesses**.

Your intuition about what's slow is probably wrong. Programmers routinely spend hours optimizing code that wasn't the bottleneck, while the actual bottleneck sits untouched. This wastes time and often makes code harder to read for no benefit.

### Simple Timing

The simplest way to measure is with a stopwatch:

```rust
bind Viper.Time;

func start() {
    var startTime = Time.millis();

    doExpensiveWork();

    var elapsed = Time.millis() - startTime;
    Viper.Terminal.Say("Took " + elapsed + " ms");
}
```

This tells you how long `doExpensiveWork` takes. But what if `doExpensiveWork` calls ten other functions? Which one is slow?

### Measuring Sections

You can time individual sections:

```rust
bind Viper.Time;

func processData(data: [Record]) {
    var t0 = Time.millis();

    var parsed = parseRecords(data);

    var t1 = Time.millis();
    Viper.Terminal.Say("Parsing: " + (t1 - t0) + " ms");

    var validated = validateRecords(parsed);

    var t2 = Time.millis();
    Viper.Terminal.Say("Validation: " + (t2 - t1) + " ms");

    var results = computeResults(validated);

    var t3 = Time.millis();
    Viper.Terminal.Say("Computation: " + (t3 - t2) + " ms");

    writeResults(results);

    var t4 = Time.millis();
    Viper.Terminal.Say("Writing: " + (t4 - t3) + " ms");
}
```

Output might look like:
```
Parsing: 150 ms
Validation: 2340 ms
Computation: 89 ms
Writing: 201 ms
```

Now you know: validation is the bottleneck. Optimizing parsing or computation would be wasted effort.

### A Timing Helper

Let's make timing reusable:

```rust
bind Viper.Time;

func timed<T>(name: string, work: func() -> T) -> T {
    var start = Time.nanos();
    var result = work();
    var elapsed = (Time.nanos() - start) / 1_000_000.0;
    Viper.Terminal.Say(name + ": " + elapsed + " ms");
    return result;
}

// Usage
func processData(data: [Record]) {
    var parsed = timed("Parse", func() {
        return parseRecords(data);
    });

    var validated = timed("Validate", func() {
        return validateRecords(parsed);
    });

    var results = timed("Compute", func() {
        return computeResults(validated);
    });

    timed("Write", func() {
        writeResults(results);
    });
}
```

This keeps your code clean while still providing timing information.

---

## Profiling: Let the Computer Find Bottlenecks

Manual timing works for small programs, but for larger ones, you need a *profiler*. A profiler automatically measures where your program spends time.

### Using Viper's Built-in Profiler

```bash
zia --profile myprogram.zia
```

After your program finishes, you'll see output like:

```
Function                  Calls      Time (ms)    % Total    Avg (ms)
───────────────────────────────────────────────────────────────────────
processImage              1000       4500         45.0%      4.50
readFile                  1000       3200         32.0%      3.20
calculateChecksum         1000       1500         15.0%      1.50
saveFile                  1000       600          6.0%       0.60
parseHeader               5000       150          1.5%       0.03
validateFormat            1000       50           0.5%       0.05
───────────────────────────────────────────────────────────────────────
Total:                               10000        100%
```

### Reading Profiler Output

Focus on the top rows. In this example:
- `processImage` takes 45% of total time. This is your primary target.
- `readFile` takes 32%. Worth investigating.
- `calculateChecksum` takes 15%. Maybe worth optimizing.
- Everything else is noise. Optimizing `parseHeader` or `validateFormat` won't help noticeably.

The "Calls" column matters too. If a function is called a million times, even tiny improvements compound. If it's called once, only dramatic improvements matter.

### Sampling vs. Instrumentation Profilers

**Sampling profilers** periodically check where your program is executing. They have low overhead but might miss very fast functions.

**Instrumentation profilers** record every function entry and exit. They're more accurate but slow your program down, which can distort results.

Viper's `--profile` uses sampling by default. For detailed analysis:

```bash
zia --profile=instrument myprogram.zia
```

### Memory Profiling

Sometimes the bottleneck isn't CPU time but memory:

```bash
zia --profile=memory myprogram.zia
```

Output shows allocations:
```
Type                Allocations    Total Size    % Memory
─────────────────────────────────────────────────────────
string              50000          4.5 MB        45%
[i64]               25000          3.2 MB        32%
Image               1000           1.5 MB        15%
Map<string, i64>    500            0.8 MB        8%
```

High allocation counts often indicate unnecessary object creation inside loops.

---

## Big O Notation: Predicting How Code Scales

Profiling tells you what's slow now. Big O notation tells you what will become slow later, as your data grows.

### The Core Idea

Big O describes how the work required grows as input size grows. It answers: "If I have 10x more data, how much longer will this take?"

Consider searching for a name in a list:

```rust
func findPerson(people: [string], name: string) -> i64 {
    for i in 0..people.length {
        if people[i] == name {
            return i;
        }
    }
    return -1;  // not found
}
```

With 100 people, you might check up to 100 names. With 1000 people, up to 1000 names. With a million people, up to a million names. The work grows *linearly* with input size. We write this as O(n), where n is the input size.

### Visualizing Growth Rates

Here's how different complexities compare:

```
Work │
  ▲  │                                         .  O(n²)
     │                                      .
     │                                   .
     │                                .
     │                            .
     │                        .
     │                   . .
     │            . . .
     │     . . . . . ──────────────────────── O(n)
     │. . ─────────────────────────────────── O(log n)
     │────────────────────────────────────── O(1)
     └──────────────────────────────────────────► Input Size
```

The differences are dramatic at large scales:

| Complexity | Name | 100 items | 10,000 items | 1,000,000 items |
|------------|------|-----------|--------------|-----------------|
| O(1) | Constant | 1 | 1 | 1 |
| O(log n) | Logarithmic | 7 | 13 | 20 |
| O(n) | Linear | 100 | 10,000 | 1,000,000 |
| O(n log n) | Log-linear | 700 | 130,000 | 20,000,000 |
| O(n²) | Quadratic | 10,000 | 100,000,000 | 1,000,000,000,000 |
| O(2^n) | Exponential | 10^30 | Impossible | Heat death of universe |

### Common Complexities Explained

**O(1) - Constant Time**

The work doesn't depend on input size. Array indexing is O(1):

```rust
func getFirst(items: [i64]) -> i64 {
    return items[0];  // Same speed whether array has 10 or 10 million items
}
```

**O(log n) - Logarithmic**

Each step eliminates half the remaining work. Binary search is O(log n):

```rust
func binarySearch(sorted: [i64], target: i64) -> i64 {
    var low = 0;
    var high = sorted.length - 1;

    while low <= high {
        var mid = (low + high) / 2;
        if sorted[mid] == target {
            return mid;
        } else if sorted[mid] < target {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return -1;
}
// 1 billion items? Only ~30 comparisons needed!
```

**O(n) - Linear**

Examine each item once. Summing an array is O(n):

```rust
func sum(items: [i64]) -> i64 {
    var total = 0;
    for item in items {
        total += item;
    }
    return total;
}
```

**O(n log n) - Log-linear**

Common in efficient sorting algorithms. Each element is touched, but processing is organized cleverly:

```rust
// Merge sort, quicksort, and other efficient sorts are O(n log n)
var sorted = items.sorted();
```

**O(n²) - Quadratic**

Nested loops over the same data. Each item interacts with every other item:

```rust
// Check all pairs
func findClosestPair(points: [Point]) -> (Point, Point) {
    var closest = (points[0], points[1]);
    var minDist = distance(closest.0, closest.1);

    for i in 0..points.length {
        for j in (i+1)..points.length {  // Nested loop!
            var dist = distance(points[i], points[j]);
            if dist < minDist {
                minDist = dist;
                closest = (points[i], points[j]);
            }
        }
    }
    return closest;
}
// 1000 points = 500,000 distance calculations
// 10,000 points = 50,000,000 distance calculations
```

### Analyzing Your Code

To determine Big O, count loops:
- No loops over input: likely O(1)
- One loop: likely O(n)
- Two nested loops: likely O(n²)
- Loop that halves data each iteration: likely O(log n)
- Sorting followed by one loop: likely O(n log n)

But watch for hidden loops:

```rust
func sneakyQuadratic(items: [string]) {
    for item in items {
        if items.contains(item.reversed()) {  // contains() is O(n)!
            Viper.Terminal.Say("Found palindrome pair");
        }
    }
}
// Looks like O(n), but contains() loops internally, making it O(n²)
```

---

## Example: Finding Duplicates

Let's see Big O in action with a real problem: checking if a list has any duplicates.

### Approach 1: Check Every Pair (O(n²))

```rust
func hasDuplicates_slow(items: [i64]) -> bool {
    for i in 0..items.length {
        for j in (i+1)..items.length {
            if items[i] == items[j] {
                return true;
            }
        }
    }
    return false;
}
```

This compares each item to every item after it. For n items, that's roughly n*n/2 comparisons.

With 1000 items: ~500,000 comparisons
With 1,000,000 items: ~500,000,000,000 comparisons (could take minutes)

### Approach 2: Sort First (O(n log n))

```rust
func hasDuplicates_medium(items: [i64]) -> bool {
    var sorted = items.sorted();  // O(n log n)

    for i in 0..(sorted.length - 1) {  // O(n)
        if sorted[i] == sorted[i + 1] {
            return true;
        }
    }
    return false;
}
```

After sorting, duplicates are adjacent. One pass finds them.

With 1000 items: ~10,000 comparisons
With 1,000,000 items: ~20,000,000 comparisons (milliseconds)

### Approach 3: Use a Set (O(n))

```rust
func hasDuplicates_fast(items: [i64]) -> bool {
    var seen = Set<i64>.create();

    for item in items {
        if seen.contains(item) {  // O(1) average
            return true;
        }
        seen.add(item);  // O(1) average
    }
    return false;
}
```

Sets provide O(1) lookup. One pass through the data, one check per item.

With 1000 items: ~1000 operations
With 1,000,000 items: ~1,000,000 operations (very fast)

### The Dramatic Difference

Let's benchmark all three:

```rust
bind Viper.Time;

func start() {
    var sizes = [1000, 10000, 100000];

    for size in sizes {
        var data = generateRandomData(size);

        Viper.Terminal.Say("\n--- Size: " + size + " ---");

        var t0 = Time.millis();
        hasDuplicates_slow(data);
        Viper.Terminal.Say("O(n²):      " + (Time.millis() - t0) + " ms");

        var t1 = Time.millis();
        hasDuplicates_medium(data);
        Viper.Terminal.Say("O(n log n): " + (Time.millis() - t1) + " ms");

        var t2 = Time.millis();
        hasDuplicates_fast(data);
        Viper.Terminal.Say("O(n):       " + (Time.millis() - t2) + " ms");
    }
}
```

Typical results:
```
--- Size: 1000 ---
O(n²):      45 ms
O(n log n): 2 ms
O(n):       0 ms

--- Size: 10000 ---
O(n²):      4500 ms
O(n log n): 25 ms
O(n):       3 ms

--- Size: 100000 ---
O(n²):      450000 ms (7+ minutes!)
O(n log n): 300 ms
O(n):       35 ms
```

The O(n) version is over 10,000x faster for 100,000 items. Algorithm choice dwarfs everything else.

---

## Common Performance Patterns

Beyond Big O, certain patterns reliably cause performance problems. Learn to spot them.

### Pattern 1: Work Inside Loops

The loop multiplies any inefficiency inside it:

**Slow:**
```rust
for i in 0..1000000 {
    var config = loadConfig();  // Reading file 1,000,000 times!
    process(data[i], config);
}
```

**Fast:**
```rust
var config = loadConfig();  // Read once
for i in 0..1000000 {
    process(data[i], config);
}
```

This applies to any "invariant" work that doesn't depend on the loop variable:
- Loading files or configuration
- Database connections
- Regex compilation
- Creating objects that don't change

### Pattern 2: Wrong Data Structure

Using the wrong data structure can turn O(1) operations into O(n):

**Slow: Searching a List**
```rust
var users: [User] = [];

func findUser(id: i64) -> User? {
    for user in users {          // O(n) - checks every user
        if user.id == id {
            return user;
        }
    }
    return null;
}
```

**Fast: Using a Map**
```rust
var users: Map<i64, User> = Map.new();

func findUser(id: i64) -> User? {
    return users.get(id);        // O(1) - direct lookup
}
```

Similarly for membership testing:

**Slow:**
```rust
var seen: [string] = [];
if !contains(seen, item) {       // O(n) each time
    seen.push(item);
}
```

**Fast:**
```rust
var seen: Set<string> = Set.new();
if !seen.contains(item) {        // O(1) each time
    seen.add(item);
}
```

**Choose data structures by how you'll use them:**

| Operation | Best Choice | O(1)? |
|-----------|-------------|-------|
| Access by index | Array | Yes |
| Lookup by key | Map | Yes |
| Check membership | Set | Yes |
| Maintain sorted order | SortedMap/Tree | O(log n) |
| First-in-first-out | Queue | Yes |
| Last-in-first-out | Stack | Yes |

### Pattern 3: String Concatenation in Loops

Strings are immutable in Viper. Each concatenation creates a new string:

**Slow:**
```rust
var result = "";
for i in 0..10000 {
    result += "item " + i + "\n";  // Creates 10,000 intermediate strings!
}
```

Each `+=` creates a new string, copies all existing content, adds the new part. This is O(n²) in total!

**Fast:**
```rust
var builder = StringBuilder.create();
for i in 0..10000 {
    builder.append("item ");
    builder.append(i);
    builder.append("\n");
}
var result = builder.toString();  // One final string
```

StringBuilder accumulates efficiently, creating only one final string.

### Pattern 4: Repeated Calculations

Don't compute the same thing twice:

**Slow:**
```rust
func processExpensiveData(items: [Item]) {
    for item in items {
        if computeExpensiveValue(item) > threshold {
            Viper.Terminal.Say("High: " + computeExpensiveValue(item));  // Computing twice!
        }
    }
}
```

**Fast:**
```rust
func processExpensiveData(items: [Item]) {
    for item in items {
        var value = computeExpensiveValue(item);  // Compute once
        if value > threshold {
            Viper.Terminal.Say("High: " + value);  // Reuse
        }
    }
}
```

### Pattern 5: Caching (Memoization)

For functions called repeatedly with the same arguments, remember results:

```rust
entity FibonacciCalculator {
    hide cache: Map<i64, i64>;

    expose func init() {
        self.cache = Map.new();
    }

    expose func calculate(n: i64) -> i64 {
        // Check cache first
        if self.cache.has(n) {
            return self.cache.get(n);
        }

        // Compute if not cached
        var result: i64;
        if n <= 1 {
            result = n;
        } else {
            result = self.calculate(n - 1) + self.calculate(n - 2);
        }

        // Store for next time
        self.cache.set(n, result);
        return result;
    }
}
```

Without cache: `fib(40)` makes billions of recursive calls, taking seconds.
With cache: `fib(40)` makes 40 calls total, completing instantly.

---

## Memory Performance

Speed isn't the only concern. Memory matters too, especially on constrained devices or when processing large data.

### The Cost of Allocation

Every allocation has overhead:
1. Finding free memory
2. Initializing the object
3. Later: garbage collecting it

In tight loops, allocations add up:

**Slow:**
```rust
func processFrames() {
    while running {
        var buffer = [i64](1000);  // Allocating every frame!
        fillBuffer(buffer);
        process(buffer);
    }
    // Creates millions of temporary arrays
}
```

**Fast:**
```rust
func processFrames() {
    var buffer = [i64](1000);  // Allocate once
    while running {
        clearBuffer(buffer);
        fillBuffer(buffer);
        process(buffer);
    }
}
```

### Avoiding Copies

Copying large data structures is expensive:

**Slow:**
```rust
func processData(data: [i64]) -> [i64] {
    var result = data.clone();  // Copies entire array!
    for i in 0..result.length {
        result[i] *= 2;
    }
    return result;
}
```

If the caller doesn't need the original, modify in place:

**Fast:**
```rust
func processData(data: [i64]) {
    for i in 0..data.length {
        data[i] *= 2;
    }
    // Modifies data directly, no copy
}
```

### Memory vs. Speed Tradeoffs

Sometimes you can trade memory for speed, or vice versa.

**Trade Memory for Speed (Caching):**
```rust
entity ImageProcessor {
    hide thumbnailCache: Map<string, Image>;

    expose func getThumbnail(path: string) -> Image {
        if self.thumbnailCache.has(path) {
            return self.thumbnailCache.get(path);  // Fast: from cache
        }
        var thumb = self.generateThumbnail(path);  // Slow: compute
        self.thumbnailCache.set(path, thumb);      // Store for later
        return thumb;
    }
}
// Uses more memory, but repeated accesses are instant
```

**Trade Speed for Memory (Lazy Loading):**
```rust
entity Document {
    hide path: string;
    hide contentLoaded: bool;
    hide content: string;

    expose func getContent() -> string {
        if !self.contentLoaded {
            self.content = File.readAll(self.path);  // Load only when needed
            self.contentLoaded = true;
        }
        return self.content;
    }
}
// Uses less memory (until accessed), but first access is slow
```

**Trade Speed for Memory (Streaming):**
```rust
// Memory-heavy: Load everything
func processFile_memory(path: string) {
    var lines = File.readAllLines(path);  // Entire file in memory!
    for line in lines {
        process(line);
    }
}

// Memory-light: Stream
func processFile_streaming(path: string) {
    var reader = BufferedReader(File.open(path, "r"));
    while !reader.eof() {
        var line = reader.readLine();  // One line at a time
        process(line);
    }
    reader.close();
}
```

Streaming uses constant memory regardless of file size.

---

## I/O Performance

I/O (input/output) operations are often the slowest part of a program. Disk access takes milliseconds; network requests take tens or hundreds of milliseconds. Compare that to CPU operations measured in nanoseconds.

### Buffer Your I/O

System calls have overhead. Minimize them:

**Slow:**
```rust
var file = File.open("data.txt", "r");
while !file.eof() {
    var char = file.readChar();  // System call per character!
    process(char);
}
```

Reading one character at a time makes thousands of system calls for a typical file.

**Fast:**
```rust
var file = BufferedReader(File.open("data.txt", "r"));
while !file.eof() {
    var line = file.readLine();  // Reads chunks internally
    process(line);
}
```

BufferedReader reads large chunks into memory, then returns pieces. Far fewer system calls.

### Batch Database Operations

Network round-trips are expensive:

**Slow:**
```rust
for item in items {
    database.insert(item);  // 1000 network round-trips!
}
```

**Fast:**
```rust
database.insertBatch(items);  // 1 network round-trip
```

Batching reduces 1000 round-trips to 1, potentially 1000x faster.

### Parallelize Independent I/O

When waiting for I/O, your CPU is idle. Do multiple operations concurrently:

**Slow: Sequential**
```rust
var data1 = Http.get(url1);  // Wait 200ms
var data2 = Http.get(url2);  // Wait 200ms
var data3 = Http.get(url3);  // Wait 200ms
// Total: 600ms
```

**Fast: Parallel**
```rust
var tasks = [
    Thread.spawn(func() { return Http.get(url1); }),
    Thread.spawn(func() { return Http.get(url2); }),
    Thread.spawn(func() { return Http.get(url3); })
];

var data1 = tasks[0].result();
var data2 = tasks[1].result();
var data3 = tasks[2].result();
// Total: ~200ms (they run simultaneously)
```

Three 200ms operations complete in 200ms total, not 600ms.

---

## A Complete Optimization Example

Let's walk through optimizing a real program step by step: counting word frequencies in a large text file.

### Step 1: The Naive Implementation

```rust
func countWords_v1(text: string) -> Map<string, i64> {
    var counts: Map<string, i64> = Map.new();

    // Split by spaces
    var words = text.split(" ");

    for word in words {
        // Normalize
        var normalized = word.toLowerCase().trim();

        if normalized.length > 0 {
            if counts.has(normalized) {
                counts.set(normalized, counts.get(normalized) + 1);
            } else {
                counts.set(normalized, 1);
            }
        }
    }

    return counts;
}
```

Let's profile it on a 10MB text file:
```
countWords_v1: 2340 ms
  - split: 450 ms (creates huge array of strings)
  - toLowerCase: 380 ms (creates new string each time)
  - trim: 290 ms (creates new string each time)
  - Map operations: 320 ms
  - Memory allocations: 1,200,000 objects
```

### Step 2: Identify Problems

1. **split()** creates a massive array of strings
2. **toLowerCase()** and **trim()** create new strings for each word
3. **Two map lookups** per word (has() then get())
4. **Memory pressure** from millions of short-lived strings

### Step 3: Fix One Thing at a Time

**Version 2: Eliminate intermediate array from split**

```rust
func countWords_v2(text: string) -> Map<string, i64> {
    var counts: Map<string, i64> = Map.new();
    var wordStart = -1;

    for i in 0..text.length {
        var isSpace = text[i] == ' ' || text[i] == '\n' || text[i] == '\t';

        if isSpace {
            if wordStart >= 0 {
                var word = text.substring(wordStart, i).toLowerCase().trim();
                if word.length > 0 {
                    counts.set(word, counts.getOrDefault(word, 0) + 1);
                }
                wordStart = -1;
            }
        } else if wordStart < 0 {
            wordStart = i;
        }
    }

    // Handle last word
    if wordStart >= 0 {
        var word = text.substring(wordStart, text.length).toLowerCase().trim();
        if word.length > 0 {
            counts.set(word, counts.getOrDefault(word, 0) + 1);
        }
    }

    return counts;
}
```

Result: **1650 ms** (30% faster). No more giant array allocation.

**Version 3: Build words without intermediate strings**

```rust
func countWords_v3(text: string) -> Map<string, i64> {
    var counts: Map<string, i64> = Map.new();
    var builder = StringBuilder.create();

    for i in 0..text.length {
        var c = text[i];
        var isSpace = c == ' ' || c == '\n' || c == '\t' || c == '\r';

        if isSpace {
            if builder.length > 0 {
                var word = builder.toString();
                counts.set(word, counts.getOrDefault(word, 0) + 1);
                builder.clear();
            }
        } else {
            // Convert to lowercase inline
            if c >= 'A' && c <= 'Z' {
                c = c + 32;
            }
            // Skip punctuation (simple trim)
            if (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') {
                builder.appendChar(c);
            }
        }
    }

    // Handle last word
    if builder.length > 0 {
        var word = builder.toString();
        counts.set(word, counts.getOrDefault(word, 0) + 1);
    }

    return counts;
}
```

Result: **890 ms** (62% faster than v1). Much fewer allocations.

### Step 4: Measure the Improvement

```
Version 1: 2340 ms  (baseline)
Version 2: 1650 ms  (30% faster)
Version 3:  890 ms  (62% faster)

Memory allocations:
Version 1: 1,200,000 objects
Version 2:   850,000 objects
Version 3:   150,000 objects
```

We achieved 2.6x speedup through systematic optimization, measuring after each change.

### Step 5: Know When to Stop

Version 3 processes 10MB in under a second. Is that fast enough? If yes, stop. Further optimization would:
- Make code harder to read
- Risk introducing bugs
- Take development time better spent elsewhere

---

## When NOT to Optimize

Premature optimization is a famous trap. Here's when to resist the urge:

### Don't Optimize Before Measuring

**Bad:**
```rust
// "I think this will be faster"
var x = ((n >> 1) & 1) ^ (n & 1);  // Cryptic bit manipulation
```

**Good:**
```rust
// Clear code first
var x = if n % 2 == 0 { 0 } else { 1 };
// Measure, and only optimize if this is actually slow
```

The compiler often generates the same machine code for both. Your "optimization" just made the code harder to read.

### Don't Optimize the Wrong Thing

If the profiler says `loadConfig()` takes 90% of time, but you optimize `processData()` instead because it looks more complex, you've wasted effort.

Always let measurements guide you.

### Don't Sacrifice Correctness

```rust
// "Optimized" code that's actually broken
func fastSum(items: [i64]) -> i64 {
    // Skip null check for speed
    var total = items[0];  // Crashes if empty!
    for i in 1..items.length {
        total += items[i];
    }
    return total;
}
```

A fast program that crashes is worse than a slow program that works.

### Don't Optimize One-Time Code

```rust
// This runs once at startup
func initializeSystem() {
    loadConfiguration();
    connectToDatabase();
    prepareCache();
}
```

Even if this takes 500ms, optimizing it would save 500ms once per program run. Not worth the effort unless startup time is critical.

### The Real Rule

**Make it work, make it right, make it fast** - in that order.

1. **Work**: Does it produce correct results?
2. **Right**: Is the code clean and maintainable?
3. **Fast**: Only now, if needed, optimize.

---

## Native Compilation

For CPU-intensive work, Viper can compile to native machine code:

```bash
# VM interpretation (default)
zia myprogram.zia

# Native compilation
zia --native myprogram.zia
```

Native code runs directly on the CPU without interpreter overhead. For computation-heavy tasks, this can be 5-50x faster.

When to use native compilation:
- Heavy numerical computation
- Image/video processing
- Simulations
- Game engines
- Compression/encryption

When VM is fine:
- I/O-bound programs (waiting on files, network)
- Simple scripts
- Interactive programs (user is the bottleneck)

---

## Benchmarking Properly

When comparing approaches, benchmark carefully. Computers are tricky; many things affect timing.

### A Proper Benchmark Function

```rust
bind Viper.Time;

func benchmark(name: string, iterations: i64, work: func()) {
    // Warm up: let JIT/caches stabilize
    for i in 0..100 {
        work();
    }

    // Measure
    var start = Time.nanos();
    for i in 0..iterations {
        work();
    }
    var end = Time.nanos();

    var totalMs = (end - start) / 1_000_000.0;
    var perIter = totalMs / iterations;

    Viper.Terminal.Say(name + ": " + perIter.format(3) + " ms/iter");
}
```

### Benchmarking Tips

1. **Warm up first**: The first runs are often slower due to caching and JIT compilation
2. **Run many iterations**: Averages out random variation
3. **Use realistic data**: Benchmark with data similar to real usage
4. **Isolate variables**: Change one thing at a time between benchmarks
5. **Watch for dead code elimination**: Make sure results are actually used

**Bad benchmark:**
```rust
func badBenchmark() {
    var start = Time.millis();
    compute(data);  // Compiler might optimize this away!
    var end = Time.millis();
    Viper.Terminal.Say("Took " + (end - start));
}
```

**Good benchmark:**
```rust
func goodBenchmark() {
    var result = 0;
    var start = Time.millis();
    for i in 0..1000 {
        result += compute(data);  // Result is used, can't be optimized away
    }
    var end = Time.millis();
    Viper.Terminal.Say("Took " + ((end - start) / 1000.0) + " ms/iter");
    Viper.Terminal.Say("Checksum: " + result);  // Actually use the result
}
```

---

## Debugging Performance Issues

When your program is slow but you're not sure why, follow this systematic approach:

### Step 1: Reproduce Consistently

First, create a test case that reliably shows the problem:

```rust
func reproduceSlowness() {
    var testData = loadTestData("large_dataset.json");

    var start = Time.millis();
    processData(testData);
    var elapsed = Time.millis() - start;

    Viper.Terminal.Say("Processing took: " + elapsed + " ms");
    // Run multiple times, should see consistent results
}
```

### Step 2: Profile to Find the Hotspot

```bash
zia --profile slowprogram.zia
```

Look at the top functions. Is it what you expected? Often it's not.

### Step 3: Narrow Down

If the profiler points to a large function, add internal timing:

```rust
func processData(data: [Record]) {
    var t0 = Time.millis();

    // Section A
    ...

    var t1 = Time.millis();
    Viper.Terminal.Say("Section A: " + (t1 - t0) + " ms");

    // Section B
    ...

    var t2 = Time.millis();
    Viper.Terminal.Say("Section B: " + (t2 - t1) + " ms");

    // etc.
}
```

### Step 4: Form a Hypothesis

Based on your measurements, hypothesize what's wrong:
- "The nested loop is O(n²)"
- "We're creating too many temporary strings"
- "The database query is slow"

### Step 5: Test Your Hypothesis

Make a targeted change and measure:

```rust
// Before (hypothesis: this Set creation is expensive)
for item in items {
    var matches = Set<i64>.create();  // Creating every iteration?
    ...
}

// After (test: reuse the Set)
var matches = Set<i64>.create();  // Create once
for item in items {
    matches.clear();
    ...
}

// Measure both versions
```

### Step 6: Verify Improvement

Don't assume your change helped. Measure again:

```
Before: 2340 ms
After:  1850 ms
Improvement: 21%
```

If it didn't help, or helped less than expected, revise your hypothesis.

### Common Culprits to Check

When debugging performance, look for these usual suspects:

1. **O(n²) or worse algorithms**: Look for nested loops
2. **Work in loops**: Anything that could be hoisted out
3. **Excessive allocations**: Creating objects inside tight loops
4. **Wrong data structure**: Lists where sets/maps would be better
5. **Unbuffered I/O**: Reading/writing one byte at a time
6. **Synchronous I/O**: Waiting for network when you could parallelize
7. **String concatenation**: Building strings with `+=` in loops
8. **Missing caches**: Recomputing expensive results

---

## The Three Languages

**Zia**
```rust
bind Viper.Time;

func benchmark(name: string, work: func()) {
    var start = Time.millis();
    for i in 0..1000 {
        work();
    }
    var elapsed = Time.millis() - start;
    Viper.Terminal.Say(name + ": " + elapsed + " ms total");
}
```

**BASIC**
```basic
SUB Benchmark(name AS STRING, work AS SUB())
    DIM start AS LONG, elapsed AS LONG
    DIM i AS INTEGER

    start = TIMER_MILLIS()
    FOR i = 1 TO 1000
        CALL work()
    NEXT i
    elapsed = TIMER_MILLIS() - start

    PRINT name; ": "; elapsed; " ms total"
END SUB
```

**Pascal**
```pascal
uses ViperTime;

procedure Benchmark(name: String; work: procedure);
var
    start, elapsed: Int64;
    i: Integer;
begin
    start := TimeMillis;
    for i := 1 to 1000 do
        work;
    elapsed := TimeMillis - start;

    WriteLn(name, ': ', elapsed, ' ms total');
end;
```

---

## Summary

Performance optimization follows a clear process:

1. **Measure first**: Profile before optimizing. Your intuition is often wrong.
2. **Find the bottleneck**: 80% of time is in 20% of code. Find that 20%.
3. **Understand the cause**: Is it algorithm complexity? Memory? I/O?
4. **Fix systematically**: Change one thing, measure, repeat.
5. **Know when to stop**: Good enough is good enough.

The optimization hierarchy (biggest impact first):
1. **Better algorithms**: O(n) beats O(n²), always
2. **Better data structures**: Maps for lookup, Sets for membership
3. **Reduce allocations**: Reuse objects, avoid copies
4. **Optimize I/O**: Buffer, batch, parallelize
5. **Native compilation**: For CPU-bound work
6. **Micro-optimizations**: Rarely needed; compiler is smart

Remember:
- **Working code first**: Make it work, make it right, then make it fast
- **Measure, don't guess**: Profilers over intuition
- **Readability matters**: Unreadable "fast" code is often neither fast nor maintainable

---

## Exercises

**Exercise 26.1 (Measure)**: Take any program you've written previously. Add timing measurements to find where it spends time. Did the results match your expectations?

**Exercise 26.2 (Profile)**: Use `zia --profile` on a program that processes a file. Identify the top 3 time-consuming functions. Which one would you optimize first and why?

**Exercise 26.3 (Big O)**: What is the time complexity of each function?

```rust
// Function A
func mystery1(n: i64) -> i64 {
    var sum = 0;
    for i in 0..n {
        for j in 0..n {
            sum += 1;
        }
    }
    return sum;
}

// Function B
func mystery2(n: i64) -> i64 {
    var sum = 0;
    for i in 0..n {
        sum += i;
    }
    return sum;
}

// Function C
func mystery3(n: i64) -> i64 {
    if n <= 1 {
        return 1;
    }
    return mystery3(n / 2) + 1;
}
```

**Exercise 26.4 (Data Structure)**: Rewrite this slow code to use appropriate data structures:

```rust
func findCommon(list1: [i64], list2: [i64]) -> [i64] {
    var common: [i64] = [];
    for item in list1 {
        for other in list2 {
            if item == other && !contains(common, item) {
                common.push(item);
            }
        }
    }
    return common;
}
```

Benchmark both versions with lists of 10,000 items.

**Exercise 26.5 (Memory)**: This code creates too many objects. Rewrite it to minimize allocations:

```rust
func generateReport(records: [Record]) -> string {
    var output = "";
    for record in records {
        output += "ID: " + record.id + "\n";
        output += "Name: " + record.name + "\n";
        output += "Value: " + record.value + "\n";
        output += "---\n";
    }
    return output;
}
```

**Exercise 26.6 (I/O)**: Convert this sequential code to parallel:

```rust
func fetchAllData(urls: [string]) -> [string] {
    var results: [string] = [];
    for url in urls {
        var data = Http.get(url);
        results.push(data);
    }
    return results;
}
```

Measure the speedup with 10 URLs that each take ~100ms to fetch.

**Exercise 26.7 (Caching)**: Implement a memoized version of this function:

```rust
func expensiveComputation(x: i64, y: i64) -> i64 {
    // Simulate expensive work
    Thread.sleep(100);
    return x * y + x + y;
}
```

Your solution should return instantly for repeated calls with the same arguments.

**Exercise 26.8 (Complete Optimization)**: Profile this word-counting function, identify all performance issues, and fix them. Document each change and its impact:

```rust
func countWordFrequencies(filePath: string) -> Map<string, i64> {
    var file = File.open(filePath, "r");
    var text = "";
    while !file.eof() {
        text += file.readChar();
    }
    file.close();

    var counts: Map<string, i64> = Map.new();
    var words = text.split(" ");

    for word in words {
        var clean = word.toLowerCase();
        if clean.length > 0 {
            var existing = 0;
            if counts.has(clean) {
                existing = counts.get(clean);
            }
            counts.set(clean, existing + 1);
        }
    }

    return counts;
}
```

**Exercise 26.9 (Challenge)**: Build a simple profiler that wraps functions and tracks:
- Call count
- Total time spent
- Average time per call
- Percentage of total program time

```rust
// Your profiler should enable code like:
var profiler = Profiler.create();
var trackedFunction = profiler.track("myFunction", originalFunction);
// ... use trackedFunction ...
profiler.report();
```

**Exercise 26.10 (Challenge)**: Implement two sorting algorithms: bubble sort O(n²) and merge sort O(n log n). Benchmark them with arrays of size 100, 1000, 10000, and 100000. Create a chart or table showing how the time difference grows.

---

*Your code runs fast enough. But does it work correctly? Next chapter: Testing your programs.*

*[Continue to Chapter 27: Testing](27-testing.md)*
