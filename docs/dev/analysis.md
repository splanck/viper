# Analysis

## CFG

On-demand helpers to query basic block successors and predecessors without
constructing an explicit graph.

```cpp
using namespace viper::analysis;
auto succ = successors(block);
auto pred = predecessors(func, block);
```

## Orders

Standard depth-first orders are available without materializing a graph.

```cpp
auto po = postOrder(fn);      // entry last
auto rpo = reversePostOrder(fn); // entry first
```

## Acyclicity & Topological Order

Cycle detection and topological sorting are available for DAG-restricted
analyses.

```cpp
bool ok = isAcyclic(fn);      // false if any cycle exists
auto topo = topoOrder(fn);    // empty if cyclic
```

These helpers gate passes like the mem2reg v2 prototype, which operates only
on acyclic control-flow graphs.

## Dominators

Computes immediate dominators using the algorithm of Cooper, Harvey, and
Kennedy ("A Simple, Fast Dominance Algorithm"). The function iterates to a
fixed point over reverse post-order, yielding a tree of parent links and
children for easy traversal. Complexity is linear in practice and worst-case
\(O(V \times E)\).

## IL utilities

Helpers in `lib/IL/Utils` provide light-weight queries on basic blocks and
instructions, such as checking block membership or retrieving a block's
terminator. They depend only on `il_core`, allowing passes to use them without
pulling in the Analysis layer.
