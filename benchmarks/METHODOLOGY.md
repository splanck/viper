# Benchmark Methodology

## Overview

The Viper benchmark suite measures execution time across 9 stress programs,
comparing Viper's native codegen against C, Rust, Lua, Python, Java, and C#.

## What Is Measured

Each benchmark is a self-contained program that performs a fixed amount of
computation and returns a checksum via process exit code. The timing measures
**total process execution time**, including:

- Process creation and dynamic linker startup
- Runtime initialization (JVM, CLR, interpreter startup)
- Computation
- Process shutdown

## Timing Method

- **Clock**: `time.monotonic()` (Python monotonic clock, microsecond resolution)
- **Metric**: Median of N iterations (default 10), after W warmup iterations (default 2)
- **Median** is used instead of mean because it is robust against outliers (e.g., GC pauses, context switches)

## Known Biases

### Process Startup (favors compiled languages)

Since timing includes process startup, languages with heavier runtimes are
penalized. Approximate startup costs on Apple M4 Max:

| Language        | Startup   | Impact on 30ms benchmark |
|-----------------|-----------|--------------------------|
| C/Rust/Viper    | ~1-2ms    | ~5%                      |
| Lua             | ~3-5ms    | ~12%                     |
| Python          | ~15-20ms  | ~55%                     |
| Java (JVM)      | ~30-60ms  | ~100-150%                |
| C# (.NET CLR)   | ~20-40ms  | ~70-100%                 |

For benchmarks over 200ms (e.g., fib_stress), startup is negligible (<3%).

### JIT Warmup (penalizes Java, C#)

Each timed iteration is a separate process invocation. JIT compilers (HotSpot,
RyuJIT) compile methods on first use and cannot carry optimization profiles
across processes. This means Java and C# are always measured in "cold start"
mode, ~10-30% slower than steady-state performance.

Professional JVM benchmarks (JMH) run within a single long-lived process with
thousands of warmup iterations. Our approach measures cold-start performance,
which is a legitimate but different metric.

### Anti-Optimization (minor, favors Viper)

C/Rust/Java/C# reference implementations add `argc-1` (or `args.length`) to
the loop bound to prevent the compiler from constant-folding the entire
computation. IL/Zia/BASIC implementations use a constant loop bound. In
practice, no compiler constant-folds a 50M-iteration loop with a complex body,
so this difference is theoretical.

## Optimization Levels

| Language     | Optimization         | Notes                          |
|--------------|----------------------|--------------------------------|
| Viper native | `-O2`                | IL optimizer + native codegen  |
| C            | `-O0`, `-O2`, `-O3`  | Apple Clang                    |
| Rust         | `-O` (= release)     | rustc                          |
| Lua          | interpreted          | Lua 5.4, no compilation        |
| Python       | interpreted          | CPython, no compilation        |
| Java         | default javac + JVM  | HotSpot JIT                    |
| C#           | Release mode         | .NET RyuJIT                    |

## Return Value Validation

All benchmarks return a checksum via process exit code (`result & 0xFF`). The
benchmark script validates that all modes produce the same return value as the
C -O2 reference. Mismatches indicate a correctness bug and are reported in the
output. **Timing comparisons are not meaningful when return values differ.**

## Benchmark Programs

| Program          | Iterations | What It Tests                          |
|------------------|------------|----------------------------------------|
| arith_stress     | 50M        | Arithmetic expression chains           |
| branch_stress    | 20M        | Modulo + conditional branching         |
| call_stress      | 10M        | Function call overhead                 |
| fib_stress       | fib(40)    | Deep recursion                         |
| inline_stress    | 50M        | Small function inlining                |
| mixed_stress     | 10M        | Arithmetic + branching + calls         |
| redundant_stress | 50M        | Constant propagation + CSE             |
| string_stress    | 500K       | String concatenation + length          |
| udiv_stress      | 50M        | Integer division by powers of 2        |

## Reproducibility

Results are appended to `benchmarks/results.jsonl` as single-line JSON with
metadata (timestamp, commit, platform, CPU). Run-to-run variance is typically
<3% for benchmarks over 50ms when using 10 iterations + 2 warmup.
