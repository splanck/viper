# Analysis Utilities

Provides basic control-flow and dominance analyses for IL functions.

## CFG

`analysis::CFG` builds predecessor and successor lists for each block and
produces a deterministic postorder of blocks. Construction visits each block and
edge once (O(N + E)). The object only references existing blocks and must not
outlive the function.

## Dominator Tree

`analysis::DominatorTree` uses the algorithm by Cooper et al. to compute
immediate dominators. The implementation iterates until convergence and is
nearly linear for typical graphs. Query `dominates(A, B)` to test dominance or
`idom(B)` for the immediate dominator.

## IL Utils

The `il::util` helpers relate instructions to blocks:

- `isInBlock(bb, inst)` checks whether an instruction resides in a block.
- `findBlock(fn, inst)` finds the block containing an instruction.

Both helpers run in linear time with respect to the number of inspected
instructions.

