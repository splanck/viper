# IL utilities

Small helpers operate directly on IL blocks and instructions without pulling in
analysis infrastructure. They provide lightweight queries for passes that only
need local inspection.

```cpp
using namespace viper::il;
if (auto *term = terminator(block)) {
    // block ends with control flow
}
```

- `belongsToBlock(I, B)` tests whether an instruction reference resides in a
  given block.
- `terminator(B)` returns the block's terminator instruction pointer or `nullptr`.
- `isTerminator(I)` checks for `br`, `cbr`, `ret`, or `trap` opcodes.

These utilities keep layering clean by avoiding a dependency on the Analysis
library for simple queries.
