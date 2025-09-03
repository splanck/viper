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
