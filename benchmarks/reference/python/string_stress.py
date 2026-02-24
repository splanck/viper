"""string_stress.py — String manipulation benchmark (50K iterations).
Equivalent to examples/il/benchmarks/string_stress.il"""
import sys

s = 0
for i in range(50000):
    t = "Hello" + " " + "World" + "!"
    s += len(t)

sys.exit(s & 0xFF)
