"""mixed_stress.py — Mixed workload benchmark (100K iterations).
Equivalent to examples/il/benchmarks/mixed_stress.il"""
import sys

def helper(x):
    return x * 3 + 7

s = 0
for i in range(100000):
    t1 = i + 1
    t2 = t1 * 2
    t3 = t2 - i
    if i % 4 == 0:
        r1 = helper(t3)
        tmp = r1 * 2
    else:
        r3 = t3 + 100
        tmp = r3 * 3
    if i % 7 == 0:
        bonus_val = helper(tmp)
        tmp = tmp + bonus_val
    s += tmp

sys.exit(s & 0xFF)
