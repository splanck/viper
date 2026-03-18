"""redundant_stress.py — Redundant computation / constant propagation benchmark (50M iterations).
Equivalent to examples/il/benchmarks/redundant_stress.il"""
import sys

s = 0
for i in range(50000000):
    k1 = 10 + 20
    k2 = k1 * 3
    k3 = k2 - 40

    a1 = i + 7
    a2 = a1 * 3

    b1 = i + 7
    b2 = b1 * 3

    c1 = 100 + 200
    c2 = c1 * 2
    c3 = c2 - 100

    d1 = 5 + 10
    d2 = d1 * 5
    d3 = d2 - 5

    live = a2 + b2 + k3 + c3 + d3

    raw_sum = s + live
    s = raw_sum & 268435455

sys.exit(s & 0xFF)
