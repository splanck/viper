# Analysis Utilities

This module provides basic control-flow analyses used by optimization passes.

## CFG

`il::analysis::CFG` builds predecessor and successor lists for each basic block
and assigns a post-order number. Construction runs a depth-first search from the
function's entry block.

- `succs(b)`: successor blocks of `b`.
- `preds(b)`: predecessor blocks of `b`.
- `postOrder(b)`: post-order index of `b` (0 is first visited).
- `rpo()`: blocks in reverse post-order.

Complexity: `O(B + E)` where `B` is number of blocks and `E` edges.

## Dominator Tree

`il::analysis::DominatorTree` computes immediate dominators using the
Cooper–Harvey–Kennedy algorithm.

- `idom(b)`: immediate dominator of `b` (`nullptr` for entry).
- `dominates(a, b)`: true if `a` dominates `b`.

Complexity: nearly linear in the size of the graph.

## Utilities

`il::util::inBlock(inst, block)` checks if an instruction resides in a block.
It performs a linear scan over the block's instruction list.

