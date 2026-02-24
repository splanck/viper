"""arith_stress.py — Arithmetic-heavy loop benchmark (500K iterations).
Equivalent to examples/il/benchmarks/arith_stress.il"""
import sys

s = 0
for i in range(500000):
    t1 = i + 1
    t2 = t1 * 2
    t3 = i + 3
    t4 = t2 + t3
    t5 = t4 * 5
    t6 = t5 - i
    t7 = t6 + 7
    t8 = t7 * 3
    t9 = t8 - 11
    s += t9

sys.exit(s & 0xFF)
