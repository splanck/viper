# CODEMAP: IL Analysis

Static analysis utilities (`src/il/analysis/`) for IL programs.

Last updated: 2026-02-05

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

| File          | Purpose                                                                     |
|---------------|-----------------------------------------------------------------------------|
| `BasicAA.hpp` | SSA-based alias analysis with alloca/param/global tracking and ModRef queries |

### BasicAA Details

- **Alias queries**: Classifies pointer pairs as NoAlias/MayAlias/MustAlias using SSA def-chain analysis
- **Base tracking**: Follows GEP/AddrOf/GAddr chains up to depth 8 to identify allocation bases
- **ModRef queries**: Priority cascade — module functions authoritative, runtime signatures as fallback
- **Caching**: Runtime signature effects cached in hash map, rebuilt on registry changes
- **Tests**: `tests/analysis/BasicAATests.cpp`, `tests/unit/il/transform/test_opt_review_basicaa.cpp`
