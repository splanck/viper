"""udiv_stress.py — Unsigned division stress benchmark (50M iterations).
Equivalent to examples/il/benchmarks/udiv_stress.il"""
import sys

s = 0
for i in range(1, 50000001):
    d1 = i // 2
    d2 = i // 4
    d3 = i // 8
    d4 = i // 16
    d5 = i // 32
    d6 = i // 64
    d7 = i // 128
    d8 = i // 256

    s7 = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8

    raw_sum = s + s7
    s = raw_sum & 268435455

sys.exit(s & 0xFF)
