# CODEMAP: IL Analysis

- **src/il/analysis/CFG.cpp**

  Builds lightweight control-flow graph queries for IL functions without materializing persistent graph objects. The utilities collect successor and predecessor blocks by inspecting branch terminators, enabling passes to traverse edges by label resolution against the active module. They also compute post-order, reverse post-order, and topological orders while skipping unreachable blocks, providing canonical iteration sequences for analyses. Dependencies include `CFG.hpp`, IL core containers (`Module`, `Function`, `Block`, `Instr`, `Opcode`), the module registration shim in this file, and standard `<queue>`, `<stack>`, and unordered container types.

- **src/il/analysis/CFG.hpp**

  Introduces lightweight control-flow graph queries that operate directly on IL modules without constructing persistent graph structures. Callers first set the active module and can then ask for successor, predecessor, post-order, or reverse-post-order traversals to drive analyses and transforms. The header also exposes acyclicity and topological ordering helpers so passes share consistent traversal contracts. Dependencies include IL core forward declarations for modules, functions, and blocks alongside the `<vector>` container.

- **src/il/analysis/Dominators.cpp**

  Implements dominator tree construction atop the CFG helpers using the Cooper–Harvey–Kennedy algorithm. The builder walks reverse post-order sequences, intersects dominance paths for each block, and records immediate dominators along with child lists for tree traversal. Query helpers like `immediateDominator` and `dominates` then provide inexpensive dominance checks for optimization and verification passes. It relies on `Dominators.hpp`, the CFG API (`reversePostOrder`, `predecessors`), IL block objects, and the standard library's unordered maps.

- **src/il/analysis/Dominators.hpp**

  Declares the `DomTree` structure that stores immediate dominator and child relationships for each block in an IL function. It provides convenience queries such as `dominates` and `immediateDominator` so optimization passes and verifiers can reason about control flow quickly. A standalone `computeDominatorTree` entry point promises a complete computation that the implementation backs with the Cooper–Harvey–Kennedy algorithm. Dependencies include IL core block/function types plus `<unordered_map>` and `<vector>` containers, and it pairs with `Dominators.cpp` which pulls in the CFG utilities.

- **src/il/analysis/BasicAA.hpp**

  Declares a minimal alias-analysis shim used by later passes to make conservative decisions about memory dependence in the absence of a full analysis. Exposes simple queries that treat most memory as potentially aliasing, suitable for early compiler phases.
