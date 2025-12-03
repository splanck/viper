# CODEMAP: IL Analysis

Static analysis utilities (`src/il/analysis/`) for IL programs.

## Control Flow

| File | Purpose |
|------|---------|
| `CFG.hpp/cpp` | CFG queries: successors, predecessors, post-order traversal |

## Dominance

| File | Purpose |
|------|---------|
| `Dominators.hpp/cpp` | Dominator tree construction (Cooper-Harvey-Kennedy algorithm) |

## Alias Analysis

| File | Purpose |
|------|---------|
| `BasicAA.hpp` | Conservative alias analysis shim |
