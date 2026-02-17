# CODEMAP: IL Analysis

Static analysis utilities (`src/il/analysis/`) for IL programs.

Last updated: 2026-02-17

## Overview

- **Total source files**: 7 (.hpp/.cpp)

## Control Flow

| File           | Purpose                                                     |
|----------------|-------------------------------------------------------------|
| `CFG.cpp`      | CFG queries implementation                                                                       |
| `CFG.hpp`      | CFG queries: successors, predecessors, post-order, reverse post-order, topological order, acyclicity; CFGContext caching layer |
| `CallGraph.cpp`| Direct-call graph implementation                                                                 |
| `CallGraph.hpp`| Direct-call graph construction and queries (direct calls only; indirect calls ignored)           |

## Dominance

| File             | Purpose                                                       |
|------------------|---------------------------------------------------------------|
| `Dominators.cpp` | Dominator tree implementation                              |
| `Dominators.hpp` | Dominator tree construction (Lengauer-Tarjan algorithm)    |

## Alias Analysis

| File          | Purpose                                                                     |
|---------------|-----------------------------------------------------------------------------|
| `BasicAA.hpp` | SSA-based alias analysis with alloca/param/global tracking and ModRef queries |

### BasicAA Details

- **Alias queries**: Classifies pointer pairs as NoAlias/MayAlias/MustAlias using SSA def-chain analysis
- **Base tracking**: Follows GEP/AddrOf/GAddr chains up to depth 8 to identify allocation bases
- **Caching**: Runtime signature effects cached in hash map, rebuilt on registry changes
- **ModRef queries**: Priority cascade — module functions authoritative, runtime signatures as fallback
- **Tests**: `src/tests/analysis/BasicAATests.cpp`, `src/tests/unit/il/transform/test_opt_review_basicaa.cpp`
