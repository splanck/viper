# Benchmark Methodology

## Overview

The Viper benchmark suite measures execution time across 16 stress programs,
comparing Viper's native codegen against C, Rust, Lua, Python, Java, and C#.
Nine are cross-language stress programs with reference implementations; seven
are codegen-quality kernels that each isolate one native code generation path
(register allocation, addressing modes, checked arithmetic, switch dispatch,
call overhead, constant division, branchy selects).

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

### Codegen-Quality Kernels

These kernels have no cross-language reference implementations; they exist to
measure specific native code generation paths and to catch regressions in
them. Each records its expected exit-code checksum in a header comment.

| Kernel           | Iterations | Codegen path it isolates               |
|------------------|------------|----------------------------------------|
| loop_sum         | 200M       | Cross-block register allocation (loop-carried values) |
| array_traverse   | 20M loads  | Scaled-index addressing + bounds checks |
| checked_arith    | 50M        | Overflow-checked arithmetic cost        |
| switch_dispatch  | 50M        | Dense 16-case switch lowering           |
| call_leaf        | 20M        | Call overhead + caller-saved traffic (callee too branchy to inline) |
| div_const        | 20M        | Signed/unsigned division by constants   |
| select_diamond   | 100M       | Unpredictable branches / if-conversion  |

## Native-Only Lane

`scripts/benchmark.sh --native-only` skips the VM, bytecode, and reference
modes and times only the native codegen backends. This is the fast iteration
lane for codegen work; a full run including VM modes validates cross-mode
checksum agreement.

On Apple Silicon, native-arm64 timing is authoritative. x86-64 executables do
not run on this host, so x86-64 timing requires an x86 machine (or Rosetta 2,
with the translation caveat noted in results); x86-64 correctness is still
validated locally through the emit-link-run and VM-vs-native differential
test suites.

## Reproducibility

Results are appended to `benchmarks/results.jsonl` as single-line JSON with
metadata (timestamp, commit, platform, CPU). Run-to-run variance is typically
<3% for benchmarks over 50ms when using 10 iterations + 2 warmup.
