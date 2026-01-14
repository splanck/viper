#!/usr/bin/env python3
import time
import subprocess
import os

BENCH_DIR = "/Users/stephen/git/viper/benchmarks"
ILC = "/Users/stephen/git/viper/build/src/tools/ilc/ilc"
RUNTIME = "/Users/stephen/git/viper/build/src/runtime/libviper_runtime.a"
FIB_N = 45

# Fibonacci benchmark - same algorithm as Viper
def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

def run_python_bench():
    start = time.perf_counter()
    result = fib(FIB_N)
    elapsed = time.perf_counter() - start
    return result, elapsed

def run_viper_native_bench():
    # Emit IL from Viper
    subprocess.run(
        ["viper", "bench.viper", "--emit-il", "-o", "bench.il"],
        capture_output=True,
        cwd=BENCH_DIR
    )

    # Compile IL to assembly
    subprocess.run(
        [ILC, "codegen", "arm64", "bench.il", "-S", "bench.s"],
        capture_output=True,
        cwd=BENCH_DIR
    )

    # Assemble and link with runtime
    subprocess.run(
        ["clang", "-c", "bench.s", "-o", "bench.o"],
        capture_output=True,
        cwd=BENCH_DIR
    )
    subprocess.run(
        ["clang", "bench.o", RUNTIME, "-o", "bench_native"],
        capture_output=True,
        cwd=BENCH_DIR
    )

    # Run native binary
    start = time.perf_counter()
    result = subprocess.run(
        ["./bench_native"],
        capture_output=True,
        text=True,
        cwd=BENCH_DIR
    )
    elapsed = time.perf_counter() - start
    return result.stdout.strip(), elapsed

if __name__ == "__main__":
    print("=" * 55)
    print("Viper Native vs Python Performance Benchmark")
    print("=" * 55)
    print(f"\nRunning fib({FIB_N}) recursive benchmark...\n")

    # Run Python
    py_result, py_time = run_python_bench()
    print(f"Python:       fib({FIB_N}) = {py_result}")
    print(f"              Time: {py_time:.3f}s")

    # Run Viper Native
    native_out, native_time = run_viper_native_bench()
    print(f"\nViper Native: {native_out or 'fib(' + str(FIB_N) + ') computed'}")
    print(f"              Time: {native_time:.3f}s")

    # Comparison
    print("\n" + "=" * 55)
    if native_time < py_time:
        ratio = py_time / native_time
        print(f"Viper Native is {ratio:.1f}x FASTER than Python")
    else:
        ratio = native_time / py_time
        print(f"Python is {ratio:.1f}x faster than Viper Native")
    print("=" * 55)
