# CODEMAP: IL Analysis

Static analysis utilities (`src/il/analysis/`) for IL programs.

Last updated: 2026-01-15

## Overview

- **Total source files**: 7 (.hpp/.cpp)

## Control Flow

| File           | Purpose                                                     |
|----------------|-------------------------------------------------------------|
| `CFG.cpp`      | CFG queries implementation                                  |
| `CFG.hpp`      | CFG queries: successors, predecessors, post-order traversal |
| `CallGraph.cpp`| Inter-procedural call graph implementation                  |
| `CallGraph.hpp`| Inter-procedural call graph construction and queries        |

## Dominance

| File             | Purpose                                                       |
|------------------|---------------------------------------------------------------|
| `Dominators.cpp` | Dominator tree implementation                                 |
| `Dominators.hpp` | Dominator tree construction (Cooper-Harvey-Kennedy algorithm) |

## Alias Analysis

| File          | Purpose                          |
|---------------|----------------------------------|
| `BasicAA.hpp` | Conservative alias analysis shim |
