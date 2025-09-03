# Analysis Utilities

This module provides basic control-flow and dominance analyses for IL functions.

## CFG

`il::analysis::CFG` builds predecessor and successor lists for each basic block
and computes a postorder numbering starting from the entry block. Construction
runs in $O(N + E)$ time.

## Dominator Tree

`il::analysis::DominatorTree` implements the simple dominator tree algorithm by
Cooper et al. using the CFG's postorder. Queries for immediate dominators and
`dominates` checks execute in $O(h)$ where $h$ is the height of the dominator
 tree.

## IL Utils

`il::utils::isInstrInBlock` tests whether a given instruction resides in a
particular basic block. This performs a linear scan over the block's instruction
list.

These utilities underpin SSA construction and other transforms such as mem2reg.

