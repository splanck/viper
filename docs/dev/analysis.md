# Analysis Utilities

This module provides basic analyses used by upcoming SSA construction.

## CFG

`il::analysis::CFG` builds predecessor and successor lists for each block in a
function and exposes a post-order traversal starting from the entry block.
Construction runs in `O(B + E)` where `B` is the number of blocks and `E` the
number of edges.

## Dominator Tree

`il::analysis::DominatorTree` computes immediate dominators using the algorithm
from "A Simple, Fast Dominance Algorithm" by Cooper et al. It iterates until
convergence in near-linear time. Query with `dominates(A, B)` or `idom(B)`.

## Utilities

`il::utils` offers helpers such as `inBlock` to test membership of an
instruction within a block and `isTerminator` to detect control-flow
instructions.

These utilities operate solely on existing IL data structures and require no
block parameters.
