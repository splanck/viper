# Analysis

## CFG

On-demand helpers to query basic block successors and predecessors without
constructing an explicit graph.

```cpp
using namespace viper::analysis;
auto succ = successors(block);
auto pred = predecessors(func, block);
```
