"""fib_stress.py — Recursive fibonacci(40) benchmark.
Equivalent to examples/il/benchmarks/fib_stress.il"""
import sys

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

sys.exit(fib(40) & 0xFF)
