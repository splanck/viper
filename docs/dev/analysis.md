# Analysis Library

## Control Flow Graph (CFG)

On-demand helpers to inspect successors and predecessors of IL basic blocks.

```cpp
using namespace viper::analysis;
auto succ = successors(fn, block);
auto pred = predecessors(fn, block);
```

