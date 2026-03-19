"""inline_stress.py — Inlining stress benchmark (50M iterations).
Equivalent to examples/il/benchmarks/inline_stress.il"""
import sys

def double_val(x):
    return x + x

def square(x):
    return x * x

def add3(a, b, c):
    return a + b + c

def inc(x):
    return x + 1

def combine(x):
    d = double_val(x)
    s = square(x)
    i = inc(x)
    return add3(d, s, i)

s = 0
for i in range(50000000):
    r = combine(i)
    raw_sum = s + r
    s = raw_sum & 268435455

sys.exit(s & 0xFF)
