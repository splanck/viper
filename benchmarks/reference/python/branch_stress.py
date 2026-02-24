"""branch_stress.py — Branch-heavy loop benchmark (200K iterations).
Equivalent to examples/il/benchmarks/branch_stress.il"""
import sys

count = 0
for i in range(200000):
    if i % 2 == 0:
        count += 1
    if i % 3 == 0:
        count += 2
    if i % 5 == 0:
        count += 3
    if i % 7 == 0:
        count += 5

sys.exit(count & 0xFF)
