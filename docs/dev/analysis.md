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

## Dominators

Computes immediate dominators using the algorithm of Cooper, Harvey, and
Kennedy ("A Simple, Fast Dominance Algorithm"). The function iterates to a
fixed point over reverse post-order, yielding a tree of parent links and
children for easy traversal. Complexity is linear in practice and worst-case
\(O(V \times E)\).
