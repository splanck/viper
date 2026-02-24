"""call_stress.py — Function call overhead benchmark (100K iterations).
Equivalent to examples/il/benchmarks/call_stress.il"""
import sys

def add_triple(a, b, c):
    return a + b + c

def mul_pair(x, y):
    return x * y

def compute(n):
    a = n
    b = n + 1
    c = n + 2
    s = add_triple(a, b, c)
    return mul_pair(s, 3)

total = 0
for i in range(100000):
    r1 = compute(i)
    r2 = add_triple(i, r1, 1)
    r3 = mul_pair(r2, 2)
    total += r3

sys.exit(total & 0xFF)
