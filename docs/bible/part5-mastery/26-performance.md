# Chapter 26: Performance

Your program works. But is it fast enough? Does it use too much memory? Performance matters — users notice slow programs.

This chapter teaches you to measure, understand, and improve performance.

---

## The First Rule: Measure First

Don't guess where time goes. Measure.

```viper
import Viper.Time;

func start() {
    var startTime = Time.millis();

    doExpensiveWork();

    var endTime = Time.millis();
    Viper.Terminal.Say("Took " + (endTime - startTime) + " ms");
}
```

Always measure before optimizing. Your intuition about slow code is often wrong.

---

## Profiling

A *profiler* shows where your program spends time:

```bash
viper --profile myprogram.viper
```

Output:
```
Function                  Calls      Time (ms)    % Total
───────────────────────────────────────────────────────────
processImage              1000       4500         45%
readFile                  1000       3200         32%
calculateChecksum         1000       1500         15%
saveFile                  1000       600          6%
other                     -          200          2%
```

Focus on the top items. Optimizing `saveFile` won't help much — it's only 6% of time.

### The 80/20 Rule

80% of time is often spent in 20% of code. Find that 20%.

---

## Algorithm Complexity

The biggest performance gains come from better algorithms, not micro-optimizations.

### Big O Notation

Describes how work grows with input size:

| O(n) | Name | Example | 1000 items | 1M items |
|------|------|---------|------------|----------|
| O(1) | Constant | Array access | 1 | 1 |
| O(log n) | Logarithmic | Binary search | 10 | 20 |
| O(n) | Linear | Loop through array | 1000 | 1M |
| O(n log n) | Log-linear | Good sorting | 10K | 20M |
| O(n²) | Quadratic | Nested loops | 1M | 1T |
| O(2ⁿ) | Exponential | Brute force | ∞ | ∞ |

### Example: Finding Duplicates

**O(n²) approach — slow:**
```viper
func hasDuplicates(items: [i64]) -> bool {
    for i in 0..items.length {
        for j in (i+1)..items.length {
            if items[i] == items[j] {
                return true;
            }
        }
    }
    return false;
}
// 1 million items → 500 billion comparisons!
```

**O(n) approach — fast:**
```viper
func hasDuplicates(items: [i64]) -> bool {
    var seen = Set<i64>.create();
    for item in items {
        if seen.contains(item) {
            return true;
        }
        seen.add(item);
    }
    return false;
}
// 1 million items → 1 million operations
```

The O(n) version is ~500,000x faster for large inputs!

---

## Common Performance Patterns

### Avoid Work in Loops

**Slow:**
```viper
for i in 0..1000000 {
    var config = loadConfig();  // Loading 1 million times!
    process(data[i], config);
}
```

**Fast:**
```viper
var config = loadConfig();  // Load once
for i in 0..1000000 {
    process(data[i], config);
}
```

### Use the Right Data Structure

**Looking up by key?**
```viper
// Slow: O(n) search
var users: [User] = [];
func findUser(id: i64) -> User? {
    for user in users {
        if user.id == id {
            return user;
        }
    }
    return null;
}

// Fast: O(1) lookup
var users: Map<i64, User> = Map.new();
func findUser(id: i64) -> User? {
    return users.get(id);
}
```

**Checking membership?**
```viper
// Slow: O(n)
var seen: [string] = [];
if !contains(seen, item) {
    seen.push(item);
}

// Fast: O(1)
var seen: Set<string> = Set.new();
if !seen.contains(item) {
    seen.add(item);
}
```

### Avoid String Concatenation in Loops

**Slow:**
```viper
var result = "";
for i in 0..10000 {
    result += "item " + i + "\n";  // Creates new string each time!
}
```

**Fast:**
```viper
var builder = StringBuilder.create();
for i in 0..10000 {
    builder.append("item ");
    builder.append(i);
    builder.append("\n");
}
var result = builder.toString();
```

### Cache Expensive Results

```viper
entity FibonacciCalculator {
    hide cache: Map<i64, i64>;

    expose func init() {
        self.cache = Map.new();
    }

    func calculate(n: i64) -> i64 {
        if self.cache.has(n) {
            return self.cache.get(n);
        }

        var result: i64;
        if n <= 1 {
            result = n;
        } else {
            result = self.calculate(n - 1) + self.calculate(n - 2);
        }

        self.cache.set(n, result);
        return result;
    }
}

// Without cache: fib(40) takes seconds
// With cache: fib(40) is instant
```

---

## Memory Performance

### Reduce Allocations

Each allocation has overhead. Reuse objects when possible:

**Slow:**
```viper
func processFrames() {
    while running {
        var buffer = [i64](1000);  // New allocation every frame!
        fillBuffer(buffer);
        process(buffer);
    }
}
```

**Fast:**
```viper
func processFrames() {
    var buffer = [i64](1000);  // Allocate once
    while running {
        clearBuffer(buffer);
        fillBuffer(buffer);
        process(buffer);
    }
}
```

### Avoid Unnecessary Copies

```viper
// Slow: copying large array
func processData(data: [i64]) -> [i64] {
    var result = data.clone();  // Full copy!
    for i in 0..result.length {
        result[i] *= 2;
    }
    return result;
}

// Fast: modify in place (if caller allows)
func processData(data: [i64]) {
    for i in 0..data.length {
        data[i] *= 2;
    }
}
```

### Watch Object Lifetimes

Objects that live too long or too short cause problems:

```viper
// Bad: Short-lived objects trigger many GCs
for i in 0..1000000 {
    var temp = createComplexObject();  // Created and discarded
    useOnce(temp);
}

// Better: Reuse when possible
var temp = createComplexObject();
for i in 0..1000000 {
    resetObject(temp);
    useOnce(temp);
}
```

---

## I/O Performance

I/O (files, network) is often the bottleneck.

### Buffer I/O

**Slow:**
```viper
var file = File.open("data.txt", "r");
while !file.eof() {
    var char = file.readChar();  // System call per character!
    process(char);
}
```

**Fast:**
```viper
var file = BufferedReader(File.open("data.txt", "r"));
while !file.eof() {
    var line = file.readLine();  // Reads chunks internally
    process(line);
}
```

### Batch Operations

**Slow:**
```viper
for item in items {
    database.insert(item);  // 1000 round-trips
}
```

**Fast:**
```viper
database.insertBatch(items);  // 1 round-trip
```

### Async I/O

Don't wait for I/O — do other work:

```viper
// Slow: Sequential
var data1 = Http.get(url1);
var data2 = Http.get(url2);
var data3 = Http.get(url3);

// Fast: Parallel
var tasks = [
    Thread.spawn(func() { return Http.get(url1); }),
    Thread.spawn(func() { return Http.get(url2); }),
    Thread.spawn(func() { return Http.get(url3); })
];
var data1 = tasks[0].result();
var data2 = tasks[1].result();
var data3 = tasks[2].result();
```

---

## Native Compilation

For CPU-intensive code, compile to native:

```bash
# VM interpretation
viper myprogram.viper

# Native compilation (faster)
viper --native myprogram.viper
```

Native code runs 5-50x faster for computation-heavy tasks.

---

## Benchmarking

Compare different approaches systematically:

```viper
import Viper.Time;

func benchmark(name: string, iterations: i64, work: func()) {
    // Warm up
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

    Viper.Terminal.Say(name + ": " + perIter + " ms/iter (" + iterations + " iterations)");
}

func start() {
    var data = generateTestData(10000);

    benchmark("Approach A", 1000, func() {
        approachA(data);
    });

    benchmark("Approach B", 1000, func() {
        approachB(data);
    });
}
```

### Benchmarking Tips

1. **Warm up**: Run a few iterations first to let JIT/caches stabilize
2. **Multiple runs**: Run multiple iterations to average out noise
3. **Realistic data**: Use data that matches real usage
4. **Isolate var iables**: Change one thing at a time

---

## A Complete Example: Optimizing Word Count

**Version 1: Naive**
```viper
func countWords(text: string) -> Map<string, i64> {
    var counts: Map<string, i64> = Map.new();

    // Split by spaces (creates many strings)
    var words = text.split(" ");

    for word in words {
        // Normalize (creates more strings)
        var normalized = word.toLowerCase().trim();

        if counts.has(normalized) {
            counts.set(normalized, counts.get(normalized) + 1);
        } else {
            counts.set(normalized, 1);
        }
    }

    return counts;
}
```

**Version 2: Optimized**
```viper
func countWords(text: string) -> Map<string, i64> {
    var counts: Map<string, i64> = Map.new();
    var builder = StringBuilder.create();

    var i = 0;
    while i < text.length {
        // Skip whitespace
        while i < text.length && isWhitespace(text[i]) {
            i += 1;
        }

        // Collect word
        builder.clear();
        while i < text.length && !isWhitespace(text[i]) {
            builder.appendChar(toLower(text[i]));
            i += 1;
        }

        if builder.length > 0 {
            var word = builder.toString();
            counts.set(word, counts.getOrDefault(word, 0) + 1);
        }
    }

    return counts;
}

func isWhitespace(c: char) -> bool {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

func toLower(c: char) -> char {
    if c >= 'A' && c <= 'Z' {
        return c + 32;
    }
    return c;
}
```

The optimized version:
- Scans once (no split creating arrays)
- Reuses StringBuilder (fewer allocations)
- Avoids intermediate strings

---

## The Three Languages

**ViperLang**
```viper
import Viper.Time;

var start = Time.millis();
doWork();
var elapsed = Time.millis() - start;
```

**BASIC**
```basic
DIM startTime AS LONG, elapsed AS LONG
startTime = TIMER_MILLIS()
CALL DoWork()
elapsed = TIMER_MILLIS() - startTime
```

**Pascal**
```pascal
uses ViperTime;
var
    startTime, elapsed: Int64;
begin
    startTime := TimeMillis;
    DoWork;
    elapsed := TimeMillis - startTime;
end.
```

---

## Common Mistakes

**Premature optimization**
```viper
// Don't do this until you've measured!
// Complex "optimized" code that's actually slower
```
Always measure first. Often the "obvious" optimization doesn't help.

**Optimizing the wrong thing**
```viper
// Profile says loadConfig takes 90% of time
// But you optimize processData instead
```
Focus on what the profiler tells you.

**Micro-optimizing at the expense of readability**
```viper
// Unreadable "optimized" code
var x = ((n >> 1) & 1) ^ (n & 1);

// Clear code (compiler optimizes it anyway)
var x = if n % 2 == 0 { 0 } else { 1 };
```
Compilers are smart. Write clear code first.

---

## Summary

1. **Measure first**: Profile before optimizing
2. **Algorithm matters most**: O(n) beats O(n²) for large inputs
3. **Use right data structures**: Maps for lookup, Sets for membership
4. **Reduce allocations**: Reuse objects, avoid copies
5. **Buffer I/O**: Batch reads/writes
6. **Parallelize I/O**: Don't wait sequentially
7. **Native compilation**: 5-50x faster for CPU-bound code
8. **Benchmark properly**: Warm up, multiple runs, realistic data

Performance optimization order:
1. Better algorithms
2. Better data structures
3. Reduce allocations
4. Optimize I/O
5. Parallelize
6. Native compilation
7. Micro-optimizations (rarely needed)

---

## Exercises

**Exercise 26.1**: Profile a program that processes a large file. Identify the bottleneck and fix it.

**Exercise 26.2**: Implement both O(n²) and O(n log n) sorting algorithms. Benchmark them with different input sizes.

**Exercise 26.3**: Optimize a string processing function that builds HTML from templates. Measure before and after.

**Exercise 26.4**: Find and fix memory allocation issues in a program that processes images in a loop.

**Exercise 26.5**: Convert a sequential web scraper to parallel. Measure the speedup.

**Exercise 26.6** (Challenge): Build a simple profiler that instruments function calls and reports timing.

---

*Your code runs fast. But does it work correctly? Next chapter: Testing your programs.*

*[Continue to Chapter 27: Testing →](27-testing.md)*
